#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/nlr.h"
#include "py/compile.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"

#include "lib/fatfs/ff.h"
#include "lib/fatfs/diskio.h"
#include "lib/utils/pyexec.h"
#include "extmod/fsusermount.h"

#include "asf/common/services/sleepmgr/sleepmgr.h"
#include "asf/common/services/usb/udc/udc.h"
#include "asf/common2/services/delay/delay.h"
#include "asf/sam0/drivers/port/port.h"
#include "asf/sam0/drivers/sercom/usart/usart.h"
#include "asf/sam0/drivers/system/system.h"
#include <board.h>

#include "autoreset.h"
#include "mpconfigboard.h"
#include "modmachine_pin.h"
#include "samdneopixel.h"
#include "tick.h"

fs_user_mount_t fs_user_mount_flash;

void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
    if (lex == NULL) {
        printf("MemoryError: lexer could not allocate memory\n");
        return;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, MP_EMIT_OPT_NONE, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // uncaught exception
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

// TODO(tannewt): Remove these default files in favor a very simple README with
// a url to all of the files that ship on boards.

static const char fresh_boot_py[] =
/*"# boot.py -- run on boot-up\r\n"*/
/*"# can run arbitrary Python, but best to keep it minimal\r\n"*/
"\r\n"
;

static const char fresh_main_py[] =
/*"# main.py -- put your code here!\r\n"*/
"\r\n"
;

static const char fresh_readme_txt[] =
/*"This is a MicroPython board\r\n"*/
/*"\r\n"*/
/*"You can get started right away by writing your Python code in 'main.py'.\r\n"*/
/*"\r\n"*/
/*"For a serial prompt:\r\n"*/
/*" - Windows: you need to go to 'Device manager', right click on the unknown device,\r\n"*/
/*"   then update the driver software, using the 'pybcdc.inf' file found on this drive.\r\n"*/
/*"   Then use a terminal program like Hyperterminal or putty.\r\n"*/
/*" - Mac OS X: use the command: screen /dev/tty.usbmodem*\r\n"*/
/*" - Linux: use the command: screen /dev/ttyACM0\r\n"*/
/*"\r\n"*/
/*"Please visit http://micropython.org/help/ for further help.\r\n"*/
"\r\n"
;

extern void flash_init_vfs(fs_user_mount_t *vfs);

// we don't make this function static because it needs a lot of stack and we
// want it to be executed without using stack within main() function
void init_flash_fs() {
    // init the vfs object
    fs_user_mount_t *vfs = &fs_user_mount_flash;
    vfs->str = "/flash";
    vfs->len = 6;
    vfs->flags = 0;
    flash_init_vfs(vfs);

    // put the flash device in slot 0 (it will be unused at this point)
    MP_STATE_PORT(fs_user_mount)[0] = vfs;

    // try to mount the flash
    FRESULT res = f_mount(&vfs->fatfs, vfs->str, 1);

    if (res == FR_NO_FILESYSTEM) {
        // no filesystem, or asked to reset it, so create a fresh one

        // We are before USB initializes so temporarily undo the USB_WRITEABLE
        // requirement.
        bool usb_writeable = (vfs->flags & FSUSER_USB_WRITEABLE) > 0;
        vfs->flags &= ~FSUSER_USB_WRITEABLE;

        res = f_mkfs("/flash", 0, 0);
        if (res == FR_OK) {
            // success creating fresh LFS
        } else {
            printf("PYB: can't create flash filesystem\n");
            MP_STATE_PORT(fs_user_mount)[0] = NULL;
            return;
        }

        // set label
        f_setlabel("MICROPYTHON");

        // create empty main.py
        FIL fp;
        f_open(&fp, "/flash/main.py", FA_WRITE | FA_CREATE_ALWAYS);
        UINT n;
        f_write(&fp, fresh_main_py, sizeof(fresh_main_py) - 1 /* don't count null terminator */, &n);
        f_close(&fp);

        // TODO(tannewt): Create an .inf driver file for Windows.

        // create readme file
        f_open(&fp, "/flash/README.txt", FA_WRITE | FA_CREATE_ALWAYS);
        f_write(&fp, fresh_readme_txt, sizeof(fresh_readme_txt) - 1 /* don't count null terminator */, &n);
        f_close(&fp);

        // Make sure we have a /flash/boot.py.  Create it if needed.
        FILINFO fno;
    #if _USE_LFN
        fno.lfname = NULL;
        fno.lfsize = 0;
    #endif
        res = f_stat("/flash/boot.py", &fno);
        if (res == FR_OK) {
            if (fno.fattrib & AM_DIR) {
                // exists as a directory
                // TODO handle this case
                // see http://elm-chan.org/fsw/ff/img/app2.c for a "rm -rf" implementation
            } else {
                // exists as a file, good!
            }
        } else {
            // doesn't exist, create fresh file

            FIL fp;
            f_open(&fp, "/flash/boot.py", FA_WRITE | FA_CREATE_ALWAYS);
            UINT n;
            f_write(&fp, fresh_boot_py, sizeof(fresh_boot_py) - 1 /* don't count null terminator */, &n);
            // TODO check we could write n bytes
            f_close(&fp);
        }
        if (usb_writeable) {
            vfs->flags |= FSUSER_USB_WRITEABLE;
        }
    } else if (res == FR_OK) {
        // mount successful
    } else {
        printf("PYB: can't mount flash\n");
        MP_STATE_PORT(fs_user_mount)[0] = NULL;
        return;
    }

    // The current directory is used as the boot up directory.
    // It is set to the internal flash filesystem by default.
    f_chdrive("/flash");
}

static char *stack_top;
static char heap[16384];

void reset_mp() {
    new_status_color(0x8f, 0x00, 0x8f);
    autoreset_stop();
    autoreset_enable();

    // Sync the file systems in case any used RAM from the GC to cache. As soon
    // as we re-init the GC all bets are off on the cache.
    disk_ioctl(0, CTRL_SYNC, NULL);
    disk_ioctl(1, CTRL_SYNC, NULL);
    disk_ioctl(2, CTRL_SYNC, NULL);

    #if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
    #endif
    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); // current dir (or base dir of the script)
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_flash_slash_lib));
    mp_obj_list_init(mp_sys_argv, 0);

    MP_STATE_PORT(mp_kbd_exception) = mp_obj_new_exception(&mp_type_KeyboardInterrupt);

    pin_init0();
}

void start_mp() {
    #ifdef AUTORESET_DELAY_MS
        mp_hal_stdout_tx_str("\r\n");
        mp_hal_stdout_tx_str("Auto-soft reset is on. Simply save files over USB to run them.\r\n");
        mp_hal_stdout_tx_str("Type anything into the REPL to disable and manually reset (CTRL-D) to re-enable.\r\n");
    #endif

    new_status_color(0x00, 0x00, 0x8f);
    mp_hal_stdout_tx_str("boot.py output:\r\n");
    int ret = pyexec_file("boot.py");
    if (ret & PYEXEC_FORCED_EXIT) {
        return;
    }

    new_status_color(0x00, 0x8f, 0x00);
    mp_hal_stdout_tx_str("\r\nmain.py output:\r\n");
    pyexec_file("main.py");
}

int main(int argc, char **argv) {
    // initialise the cpu and peripherals
    #if MICROPY_MIN_USE_SAMD21_MCU
    void samd21_init(void);
    samd21_init();
    #endif


    int stack_dummy;
    // Store the location of stack_dummy as an approximation for the top of the
    // stack so the GC can account for objects that may be referenced by the
    // stack between here and where gc_collect is called.
    stack_top = (char*)&stack_dummy;
    reset_mp();

    // Initialise the local flash filesystem after the gc in case we need to
    // grab memory from it. Create it if needed, mount in on /flash, and set it
    // as current dir.
    init_flash_fs();

    // Start USB after getting everything going.
    #ifdef USB_REPL
        udc_start();
    #endif

    // Run boot and main.
    start_mp();

    // Main script is finished, so now go into REPL mode.
    // The REPL mode can change, or it can request a soft reset.
    int exit_code = 0;
    for (;;) {
        new_status_color(0x3f, 0x3f, 0x3f);
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            exit_code = pyexec_raw_repl();
        } else {
            exit_code = pyexec_friendly_repl();
        }
        if (exit_code == PYEXEC_FORCED_EXIT) {
            mp_hal_stdout_tx_str("soft reboot\r\n");
            reset_mp();
            start_mp();
        } else if (exit_code != 0) {
            break;
        }
    }
    mp_deinit();
    return 0;
}

void gc_collect(void) {
    // WARNING: This gc_collect implementation doesn't try to get root
    // pointers from CPU registers, and thus may function incorrectly.
    void *dummy;
    gc_collect_start();
    // This naively collects all object references from an approximate stack
    // range.
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
    gc_dump_info();
}

mp_lexer_t *fat_vfs_lexer_new_from_file(const char *filename);
mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    #if MICROPY_VFS_FAT
    return fat_vfs_lexer_new_from_file(filename);
    #else
    (void)filename;
    return NULL;
    #endif
}

mp_import_stat_t fat_vfs_import_stat(const char *path);
mp_import_stat_t mp_import_stat(const char *path) {
    #if MICROPY_VFS_FAT
    return fat_vfs_import_stat(path);
    #else
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
    #endif
}

void mp_keyboard_interrupt(void) {
    MP_STATE_VM(mp_pending_exception) = MP_STATE_PORT(mp_kbd_exception);
}

void nlr_jump_fail(void *val) {
}

void NORETURN __fatal_error(const char *msg) {
    while (1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif

#if MICROPY_MIN_USE_SAMD21_MCU

#ifdef UART_REPL
struct usart_module usart_instance;
#endif

#ifdef ENABLE_MICRO_TRACE_BUFFER
// Stores 2 ^ TRACE_BUFFER_MAGNITUDE_PACKETS packets.
// 7 -> 128 packets
#define TRACE_BUFFER_MAGNITUDE_PACKETS 7
// Size in uint32_t. Two per packet.
#define TRACE_BUFFER_SIZE (1 << (TRACE_BUFFER_MAGNITUDE_PACKETS + 1))
// Size in bytes. 4 bytes per uint32_t.
#define TRACE_BUFFER_SIZE_BYTES (TRACE_BUFFER_SIZE << 2)
__attribute__((__aligned__(TRACE_BUFFER_SIZE_BYTES))) uint32_t mtb[TRACE_BUFFER_SIZE];
#endif

// Serial number as hex characters.
char serial_number[USB_DEVICE_GET_SERIAL_NAME_LENGTH];
void load_serial_number(void) {
    char nibble_to_hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A',
        'B', 'C', 'D', 'E', 'F'};
    uint32_t* addresses[4] = {(uint32_t *) 0x0080A00C, (uint32_t *) 0x0080A040,
                              (uint32_t *) 0x0080A044, (uint32_t *) 0x0080A048};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            uint8_t nibble = (*(addresses[i]) >> j * 4) & 0xf;
            serial_number[i * 8 + j] = nibble_to_hex[nibble];
        }
    }
}

void samd21_init(void) {
#ifdef ENABLE_MICRO_TRACE_BUFFER
    REG_MTB_POSITION = ((uint32_t) (mtb - REG_MTB_BASE)) & 0xFFFFFFF8;
    REG_MTB_FLOW = (((uint32_t) mtb - REG_MTB_BASE) + TRACE_BUFFER_SIZE_BYTES) & 0xFFFFFFF8;
    REG_MTB_MASTER = 0x80000000 + (TRACE_BUFFER_MAGNITUDE_PACKETS - 1);
#endif

    load_serial_number();

    irq_initialize_vectors();
    cpu_irq_enable();

    // Initialize the sleep manager
    sleepmgr_init();

    system_init();

    delay_init();

    board_init();

    // Configure millisecond timer initialization.
    tick_init();

    // Uncomment to init PIN_PA17 for debugging.
    // struct port_config pin_conf;
    // port_get_config_defaults(&pin_conf);
    //
    // pin_conf.direction  = PORT_PIN_DIR_OUTPUT;
    // port_pin_set_config(MICROPY_HW_LED1, &pin_conf);
    // port_pin_set_output_level(MICROPY_HW_LED1, false);


    #ifdef MICROPY_HW_NEOPIXEL
        struct port_config pin_conf;
        port_get_config_defaults(&pin_conf);

        pin_conf.direction  = PORT_PIN_DIR_OUTPUT;
        port_pin_set_config(MICROPY_HW_NEOPIXEL, &pin_conf);
        port_pin_set_output_level(MICROPY_HW_NEOPIXEL, false);
    #endif
}

#endif
