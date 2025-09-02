#include "noftypes.h"
#include "nes_mmc.h"
#include "nes.h"
#include "libsnss.h"
#include "nes_rom.h"
#include "nes_ppu.h"
#include <string.h>
#include <stdio.h>
#include "nes6502.h"  

#define TRACE_MMC3 0
#define TRACE_MMC3_STATS 0  /* Set to 1 to enable per-frame statistics logging */

#ifdef TRACE_MMC3
#   include <stdio.h>
#   define LOG(...)  do{ fprintf(stderr,__VA_ARGS__);}while(0)
#else
#   define LOG(...)  ((void)0)
#endif

#if TRACE_MMC3_STATS
#   include <stdio.h>
#   define STATS_LOG(...)  do{ fprintf(stderr,__VA_ARGS__);}while(0)
#else
#   define STATS_LOG(...)  ((void)0)
#endif

#ifndef INLINE
# define INLINE static inline
#endif

/* ───────────────── configuration ──────────────────────────────────── */
#define MAP4_PPU_EDGE_IRQ  1   /* 1 = proper A12 edge, 0 = HBlank tick */

/* ───────────────────────── state ──────────────────────────────────── */
typedef struct {
    uint8 counter, latch;
    bool  enabled;
    bool  reload_flag;
} irq_t;

static irq_t  irq;
static uint8  reg8000;          /* last $8000 value               */
static uint16 vrombase;         /* 0x0000 or 0x1000               */
static uint8  prg_bank6;        /* last R6 value                  */
static uint8  r7_prg_bank;      /* last R7 value (0xA000 bank)    */
static bool   fourscreen;
static bool   wram_en, wram_wp;
static uint8  wram_bank; 
static uint8 chr_reg[6];


//extern uint32 ppu_dotcycle;
static uint8 prev_a12      = 0;
static int    last_scan    = -1;  /* helps us detect the start of a frame */
static bool   scanline_a12_generated[262];  /* tracks which scanlines generated A12 edges */

#if TRACE_MMC3_STATS
/* Per-frame statistics tracking (zero overhead when disabled) */
static struct {
    int frame_count;
    int a12_edges_natural;      /* A12 edges from real PPU accesses */
    int a12_edges_synthetic;    /* A12 edges generated synthetically */
    int irq_count;              /* Total IRQs triggered */
    int irq_enable_changes;     /* Number of times IRQ was enabled/disabled */
    int irq_currently_enabled;  /* Current IRQ enable state */
} stats;
#endif



#define FIXED_LAST(c)   ((c)->rom_banks*2-1)
#define FIXED_PENULT(c) ((c)->rom_banks*2-2)

/* ─────────────── IRQ helper ───────────────────────────────────────── */
/* Call once on every rising edge of PPU-A12 */
//
INLINE void map4_clock_irq(void)
{
    // If a reload is pending or the counter is already at zero,
    // perform a reload. This action never triggers an IRQ.
    if (irq.reload_flag || irq.counter == 0) 
    {
        irq.counter     = irq.latch;
        irq.reload_flag = false;
    } 
    // Otherwise, decrement the counter.
    else 
    {
        irq.counter--;
        // An IRQ is ONLY triggered as a result of the counter being DECREMENTED to 0.
        if (irq.counter == 0 && irq.enabled) 
        {
            nes_irq();
#if TRACE_MMC3_STATS
            stats.irq_count++;
#endif
        }
    }
}


/* ─────────────── PPU bus hook ─────────────────────────────────────── */
#if MAP4_PPU_EDGE_IRQ
/* ─────────────── PPU bus hook ───────────────────────────────────────
   Call exactly once for every PPU memory access.  Only PRG-CHR reads
   (addr & 0x2000 == 0) matter for MMC3 IRQ timing.                    */
void map4_ppu_tick(uint16 addr)
{
    uint8 curr_a12 = (addr & 0x1000) ? 1 : 0;   /* current A12 level */
   
    if (!prev_a12 && curr_a12){
        map4_clock_irq();
        
        /* Mark this scanline as having generated an A12 edge */
        nes_t *nes = nes_getcontextptr();
        if (nes && nes->scanline >= 0 && nes->scanline < 262) {
            scanline_a12_generated[nes->scanline] = true;
        }
        
#if TRACE_MMC3_STATS
        stats.a12_edges_natural++;
#endif
    }

    prev_a12 = curr_a12;
}
#endif /* MAP4_PPU_EDGE_IRQ */


/* ─────────────── CPU write handler ────────────────────────────────── */
static void map4_write(uint32 a, uint8 v)
{
    switch (a & 0xE001)
    {
    /* $8000 – bank select ------------------------------------------------*/
    case 0x8000: {
        uint8 old_d7   = reg8000 & 0x80;
        uint8 old_mode = reg8000 & 0x40;      /* D6: PRG mode bit        */

        reg8000  = v;
        vrombase = (v & 0x80) ? 0x1000 : 0x0000;

        /* ─── refresh all six CHR windows if D7 toggled ─── */
        if (old_d7 != (v & 0x80)) {
            /* R0 / R1 = 2 KiB even‑only windows */
            uint8 r0 = chr_reg[0] & 0xFE;
            uint8 r1 = chr_reg[1] & 0xFE;
            mmc_bankvrom(1, vrombase ^ 0x0000, r0);
            mmc_bankvrom(1, vrombase ^ 0x0400, r0 + 1);
            mmc_bankvrom(1, vrombase ^ 0x0800, r1);
            mmc_bankvrom(1, vrombase ^ 0x0C00, r1 + 1);

            /* R2–R5 = four 1 KiB windows */
            mmc_bankvrom(1, vrombase ^ 0x1000, chr_reg[2]);
            mmc_bankvrom(1, vrombase ^ 0x1400, chr_reg[3]);
            mmc_bankvrom(1, vrombase ^ 0x1800, chr_reg[4]);
            mmc_bankvrom(1, vrombase ^ 0x1C00, chr_reg[5]);
        }

        /* fix penultimate bank (always visible) */
        mmc_bankrom(8, (v & 0x40) ? 0x8000 : 0xC000,
                       FIXED_PENULT(mmc_getinfo()));

        /* swap R6 target if PRG mode bit (D6) flipped */
        if (old_mode != (v & 0x40))
            mmc_bankrom(8, (v & 0x40) ? 0xC000 : 0x8000, prg_bank6);
        break;
    }

    /* $8001 – bank data --------------------------------------------------*/
    case 0x8001: {
        switch (reg8000 & 7)
        {
        case 0: v &= 0xFE;
                chr_reg[0] = v;
                mmc_bankvrom(1, vrombase ^ 0x0000, v);
                mmc_bankvrom(1, vrombase ^ 0x0400, v+1);      break;
        case 1: v &= 0xFE;
                chr_reg[1] = v;
                mmc_bankvrom(1, vrombase ^ 0x0800, v);
                mmc_bankvrom(1, vrombase ^ 0x0C00, v+1);      break;
        case 2: chr_reg[2] = v;  mmc_bankvrom(1, vrombase ^ 0x1000, v);        break;
        case 3: chr_reg[3] = v; mmc_bankvrom(1, vrombase ^ 0x1400, v);        break;
        case 4: chr_reg[4] = v; mmc_bankvrom(1, vrombase ^ 0x1800, v);        break;
        case 5: chr_reg[5] = v;mmc_bankvrom(1, vrombase ^ 0x1C00, v);        break;
        case 6: prg_bank6 = v;
                mmc_bankrom(8, (reg8000 & 0x40) ? 0xC000 : 0x8000, prg_bank6);
                break;
        case 7: r7_prg_bank = v; mmc_bankrom(8, 0xA000, v);  break;
        }
        break;
    }

    /* $A000 – nametable mirroring --------------------------------------- */
    case 0xA000:
        if (!fourscreen)
            (v & 1) ? ppu_mirror(0,0,1,1) : ppu_mirror(0,1,0,1);    
            

        break;

    /* $A001 – WRAM enable/protect --------------------------------------- */
    case 0xA001:
        wram_en = !!(v & 0x80);
        wram_wp = !!(v & 0x40);
        nes_set_wram_enable(wram_en);
        nes_set_wram_write_protect(wram_wp);
        mmc_bankwram(8, 0x6000, v & 3);
        break;

    /* $C000 – IRQ latch -------------------------------------------------- */
    case 0xC000: irq.latch = v; 
    #ifdef TRACE_MMC3  
        //LOG("map4_write: $C000 → set latch to ", v, "\n"); 
    #endif
    break;

    /* $C001 – IRQ reload ------------------------------------------------- */
    case 0xC001: irq.reload_flag = true;  //irq.counter = 0;
    #ifdef TRACE_MMC3  
        //LOG("map4_write: $C001 → reload\n"); 
    #endif
    break;
    /* $E000 – IRQ disable / ack ----------------------------------------- */
    case 0xE000: 
        irq.enabled = false; 
        nes_irq_ack();
#if TRACE_MMC3_STATS
        if (stats.irq_currently_enabled) {
            stats.irq_enable_changes++;
            stats.irq_currently_enabled = 0;
        }
#endif
    #ifdef TRACE_MMC3  
        //LOG("map4_write: $E000 → IRQ disable + ack\n"); 
    #endif
        break;

    /* $E001 – IRQ enable ------------------------------------------------- */
    case 0xE001: 
        irq.enabled = true;
#if TRACE_MMC3_STATS
        if (!stats.irq_currently_enabled) {
            stats.irq_enable_changes++;
            stats.irq_currently_enabled = 1;
        }
#endif
        #ifdef TRACE_MMC3 
            //LOG("map4_write: $E001 → IRQ enable\n");
        #endif
        break;
    }
    /*#ifdef TRACE_MMC3
        if ((a & 0xE001) == 0xC000 || (a & 0xE001) == 0xC001 ||
            (a & 0xE001) == 0xE000 || (a & 0xE001) == 0xE001)
        {
            LOG("WR %04X <- %02X | cnt=%02X lat=%02X rf=%d en=%d line=%d\n",
                (uint16)(a & 0xE001), v,
                irq.counter, irq.latch, irq.reload_flag, irq.enabled,
                ext_irq_line);
        }
    #endif */
    
}

/* ─────────────── HBlank fallback (edge IRQ off) ───────────────────── */
static void map4_hblank(int vblank)
{
#if !MAP4_PPU_EDGE_IRQ
    if (!vblank)
        map4_clock_irq();
#else
    nes_t *nes = nes_getcontextptr();
    if (!nes) return;
    
    /* Generate synthetic A12 edges for scanlines that didn't generate them */
    if (!vblank && nes->scanline >= 0 && nes->scanline < 240) {
        /* If this visible scanline hasn't generated an A12 edge, generate a synthetic one */
        if (!scanline_a12_generated[nes->scanline]) {
            map4_clock_irq();
            scanline_a12_generated[nes->scanline] = true;
#if TRACE_MMC3_STATS
            stats.a12_edges_synthetic++;
#endif
        }
    }
    
    /* At end of frame (scanline 261), ensure we have 240 A12 edges minimum */
    if (vblank && nes->scanline == 261) {
        /* Count actual edges generated for visible scanlines */
        int edge_count = 0;
        for (int i = 0; i < 240; i++) {
            if (scanline_a12_generated[i]) {
                edge_count++;
            }
        }
        
        /* Generate synthetic edges for any missing scanlines to reach 240 minimum */
        for (int i = 0; i < 240 && edge_count < 240; i++) {
            if (!scanline_a12_generated[i]) {
                map4_clock_irq();
                edge_count++;
#if TRACE_MMC3_STATS
                stats.a12_edges_synthetic++;
#endif
            }
        }
        
#if TRACE_MMC3_STATS
        /* Log per-frame statistics */
        stats.frame_count++;
        int total_a12_edges = stats.a12_edges_natural + stats.a12_edges_synthetic;
        STATS_LOG("Frame %d: A12_edges=%d (nat=%d,syn=%d) IRQs=%d IRQ_changes=%d IRQ_enabled=%s\n",
                  stats.frame_count,
                  total_a12_edges,
                  stats.a12_edges_natural,
                  stats.a12_edges_synthetic,
                  stats.irq_count,
                  stats.irq_enable_changes,
                  stats.irq_currently_enabled ? "YES" : "NO");
        
        /* Reset per-frame counters */
        stats.a12_edges_natural = 0;
        stats.a12_edges_synthetic = 0;
        stats.irq_count = 0;
        stats.irq_enable_changes = 0;
#endif
        
        /* Clear the tracking array for next frame */
        memset(scanline_a12_generated, false, sizeof(scanline_a12_generated));
    }
#endif
}

/* ─────────────── Save-state helpers ───────────────────────────────── */
static void map4_getstate(SnssMapperBlock *s)
{
    s->extraData.mapper4.irqCounter        = irq.counter;
    s->extraData.mapper4.irqLatchCounter   = irq.latch;
    s->extraData.mapper4.irqCounterEnabled = irq.enabled;
    s->extraData.mapper4.last8000Write     = reg8000;
    s->extraData.mapper4.fill1[0]          = irq.reload_flag;
    s->extraData.mapper4.fill1[1]          = wram_en;
    s->extraData.mapper4.fill1[2]          = wram_wp;
    s->extraData.mapper4.fill1[3]          = prg_bank6;
    s->extraData.mapper4.fill1[4]          = r7_prg_bank;
    /* Save CHR registers in remaining fill space */
    memcpy(&s->extraData.mapper4.fill1[5], chr_reg, 6);
}

static void map4_setstate(SnssMapperBlock *s)
{
    irq.counter      = s->extraData.mapper4.irqCounter;
    irq.latch        = s->extraData.mapper4.irqLatchCounter;
    irq.enabled      = s->extraData.mapper4.irqCounterEnabled;
    reg8000          = s->extraData.mapper4.last8000Write;
    irq.reload_flag  = s->extraData.mapper4.fill1[0];
    wram_en          = s->extraData.mapper4.fill1[1];
    wram_wp          = s->extraData.mapper4.fill1[2];
    prg_bank6        = s->extraData.mapper4.fill1[3];
    r7_prg_bank      = s->extraData.mapper4.fill1[4];
    /* Restore CHR registers from remaining fill space */
    memcpy(chr_reg, &s->extraData.mapper4.fill1[5], 6);

    nes_set_wram_enable(wram_en);
    nes_set_wram_write_protect(wram_wp);
    
    /* Restore banking configuration */
    vrombase = (reg8000 & 0x80) ? 0x1000 : 0x0000;
    
    /* Restore PRG banks */
    mmc_bankrom(8, (reg8000 & 0x40) ? 0xC000 : 0x8000, prg_bank6);
    mmc_bankrom(8, (reg8000 & 0x40) ? 0x8000 : 0xC000, FIXED_PENULT(mmc_getinfo()));
    mmc_bankrom(8, 0xA000, r7_prg_bank);
    mmc_bankrom(8, 0xE000, FIXED_LAST(mmc_getinfo()));
    
    /* Restore CHR banks */
    uint8 r0 = chr_reg[0] & 0xFE;
    uint8 r1 = chr_reg[1] & 0xFE;
    mmc_bankvrom(1, vrombase ^ 0x0000, r0);
    mmc_bankvrom(1, vrombase ^ 0x0400, r0 + 1);
    mmc_bankvrom(1, vrombase ^ 0x0800, r1);
    mmc_bankvrom(1, vrombase ^ 0x0C00, r1 + 1);
    mmc_bankvrom(1, vrombase ^ 0x1000, chr_reg[2]);
    mmc_bankvrom(1, vrombase ^ 0x1400, chr_reg[3]);
    mmc_bankvrom(1, vrombase ^ 0x1800, chr_reg[4]);
    mmc_bankvrom(1, vrombase ^ 0x1C00, chr_reg[5]);
}

/* ─────────────── Power-on / reset ─────────────────────────────────── */
static void map4_init(void)
{
     rominfo_t *cart = mmc_getinfo();
     memset(&irq,    0, sizeof irq);
     memset(chr_reg, 0, sizeof chr_reg);
     memset(scanline_a12_generated, false, sizeof(scanline_a12_generated));
     wram_bank = 0;
#if MAP4_PPU_EDGE_IRQ
    prev_a12 = 0; 
#endif

#if TRACE_MMC3_STATS
    /* Initialize statistics tracking */
    memset(&stats, 0, sizeof(stats));
    STATS_LOG("MMC3 statistics logging enabled\n");
#endif
    reg8000    = 0;  vrombase = 0;  prg_bank6 = 0;  r7_prg_bank = 0;
    fourscreen = !!(cart->flags & ROM_FLAG_FOURSCREEN);
    wram_en    = false; wram_wp = false;

    /* PRG layout */
    mmc_bankrom(8, 0xC000, FIXED_PENULT(cart));
    mmc_bankrom(8, 0xE000, FIXED_LAST(cart));
    mmc_bankrom(8, 0x8000, prg_bank6);
    mmc_bankrom(8, 0xA000, 0);

    /* CHR layout */
    mmc_bankvrom(8, 0x0000, 0);

#if MAP4_PPU_EDGE_IRQ
    ppu_set_mapper_hook(map4_ppu_tick);
#endif
}

/* ─────────────── memory-write table & public iface ────────────────── */
static map_memwrite map4_memwrite[] = {
    { 0x8000, 0xFFFF, map4_write },
    {    -1,     -1, NULL }
};

mapintf_t map4_intf = {
    4, "MMC3",
    map4_init, NULL,
    map4_hblank,
    map4_getstate, map4_setstate,
    NULL, map4_memwrite, NULL
};
