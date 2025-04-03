#include "raylib.h"
#include "zforth.h"
#include <errno.h>
#include <math.h>
#include <raygui.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <zfconf.h>

#ifndef RAYGUI_IMPLEMENTATION
#define RAYGUI_IMPLEMENTATION
#endif

char core[] = "\
: emit    0 sys ; \
: .       1 sys ; \
: tell    2 sys ; \
: quit    128 sys ; \
: sin     129 sys ; \
: include 130 sys ; \
: save    131 sys ; \
: init_window  132 sys ; \
: set_target_fps 133 sys ; \
: begin_drawing 134 sys ; \
: end_drawing 135 sys ; \
: clear_background 136 sys ; \
: window_should_close 137 sys ; \
: draw_text 138 sys ; \
: !    0 !! ; \
: @    0 @@ ; \
: ,    0 ,, ; \
: #    0 ## ; \
: !j	 64 !! ; \
: ,j	 64 ,, ; \
: [ 0 compiling ! ; immediate \
: ] 1 compiling ! ; \
: postpone 1 _postpone ! ; immediate \
: 1+ 1 + ; \
: 1- 1 - ; \
: over 1 pick ; \
: +!   dup @ rot + swap ! ; \
: inc  1 swap +! ; \
: dec  -1 swap +! ; \
: <    - <0 ; \
: >    swap < ; \
: <=   over over >r >r < r> r> = + ; \
: >=   swap <= ; \
: =0   0 = ; \
: not  =0 ; \
: !=   = not ; \
: cr   10 emit ; \
: br 32 emit ; \
: ..   dup . ; \
: here h @ ; \
: allot  h +!  ; \
: var : ' lit , here 5 allot here swap ! 5 allot postpone ; ; \
: const : ' lit , , postpone ; ; \
: constant >r : r> postpone literal postpone ; ; \
: variable >r here r> postpone , constant ; \
: begin   here ; immediate \
: again   ' jmp , , ; immediate \
: until   ' jmp0 , , ; immediate \
: { ( -- ) ' lit , 0 , ' >r , here ; immediate \
: x} ( -- ) ' r> , ' 1+ , ' dup , ' >r , ' = , postpone until ' r> , ' drop , ; immediate \
: exe ( XT -- ) ' lit , here dup , ' >r , ' >r , ' exit , here swap ! ; immediate \
: times ( XT n -- ) { >r dup >r exe r> r> dup x} drop drop ; \
: if      ' jmp0 , here 0 ,j ; immediate \
: unless  ' not , postpone if ; immediate \
: else    ' jmp , here 0 ,j swap here swap !j ; immediate \
: fi      here swap !j ; immediate \
: i ' lit , 0 , ' pickr , ; immediate \
: j ' lit , 2 , ' pickr , ; immediate \
: do ' swap , ' >r , ' >r , here ; immediate \
: loop+ ' r> , ' + , ' dup , ' >r , ' lit , 1 , ' pickr , ' >= , ' jmp0 , , ' r> , ' drop , ' r> , ' drop , ; immediate \
: loop ' lit , 1 , postpone loop+ ;  immediate \
: s\" 0 , compiling @ if ' lits , here 0 , fi here begin key dup 34 = if drop \
     compiling @ if here swap - swap ! else dup here swap - fi exit else , fi \
     again ; immediate \
: .\" compiling @ if postpone s\" ' tell , else begin key dup 34 = if drop exit else emit fi again \
     fi ; immediate \
: s. dsp @ 0 do dsp @ i - 1 - pick . loop ; \
: mainloop ; ";
zf_ctx *ctx;
int inputline = 0;
int running = 0;
int sigwinch_received;
int should_close = 0;

zf_result do_eval(zf_ctx *ctx, const char *src, int line, const char *buf) {
  const char *msg = NULL;

  zf_result rv = zf_eval(ctx, buf);

  switch (rv) {
  case ZF_OK:
    break;
  case ZF_ABORT_INTERNAL_ERROR:
    msg = "internal error";
    break;
  case ZF_ABORT_OUTSIDE_MEM:
    msg = "outside memory";
    break;
  case ZF_ABORT_DSTACK_OVERRUN:
    msg = "dstack overrun";
    break;
  case ZF_ABORT_DSTACK_UNDERRUN:
    msg = "dstack underrun";
    break;
  case ZF_ABORT_RSTACK_OVERRUN:
    msg = "rstack overrun";
    break;
  case ZF_ABORT_RSTACK_UNDERRUN:
    msg = "rstack underrun";
    break;
  case ZF_ABORT_NOT_A_WORD:
    msg = "not a word";
    break;
  case ZF_ABORT_COMPILE_ONLY_WORD:
    msg = "compile-only word";
    break;
  case ZF_ABORT_INVALID_SIZE:
    msg = "invalid size";
    break;
  case ZF_ABORT_DIVISION_BY_ZERO:
    msg = "division by zero";
    break;
  default:
    msg = "unknown error";
  }

  if (msg) {
    fprintf(stderr, "\033[31m");
    if (src)
      fprintf(stderr, "%s:%d: ", src, line);
    fprintf(stderr, "%s\033[0m\n", msg);
  }

  return rv;
}

void zf_include(zf_ctx *ctx, const char *fname) {
  char buf[256];

  FILE *f = fopen(fname, "rb");
  int line = 1;
  if (f) {
    while (fgets(buf, sizeof(buf), f)) {
      do_eval(ctx, fname, line++, buf);
    }
    fclose(f);
  } else {
    fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
  }
}

static void save(zf_ctx *ctx, const char *fname) {
  size_t len;
  void *p = zf_dump(ctx, &len);
  FILE *f = fopen(fname, "wb");
  if (f) {
    fwrite(p, 1, len, f);
    fclose(f);
  }
}

extern zf_input_state zf_host_sys(zf_ctx *ctx, zf_syscall_id id,
                                  const char *input) {
  switch ((int)id) {

    /* The core system callbacks */

  case ZF_SYSCALL_EMIT:
    putchar((char)zf_pop(ctx));
    fflush(stdout);
    break;

  case ZF_SYSCALL_PRINT:
    printf(ZF_CELL_FMT " ", zf_pop(ctx));
    fflush(stdout);
    break;

  case ZF_SYSCALL_TELL: {
    zf_cell len = zf_pop(ctx);
    zf_cell addr = zf_pop(ctx);
    if (addr >= ZF_DICT_SIZE - len) {
      zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
    }
    void *buf = (uint8_t *)zf_dump(ctx, NULL) + (int)addr;
    (void)fwrite(buf, 1, len, stdout);
    fflush(stdout);
  } break;

    /* Application specific callbacks */

  case ZF_SYSCALL_USER + 0:
    printf("\n");
    should_close = 1;
    break;

  case ZF_SYSCALL_USER + 1:
    zf_push(ctx, sin(zf_pop(ctx)));
    break;

  case ZF_SYSCALL_USER + 2:
    if (input == NULL) {
      return ZF_INPUT_PASS_WORD;
    }
    zf_include(ctx, input);
    break;

  case ZF_SYSCALL_USER + 3:
    save(ctx, "zforth.save");
    break;
  case ZF_SYSCALL_USER + 4: {
    zf_cell addr = zf_pop(ctx);
    size_t diclen;
    if (addr >= ZF_DICT_SIZE - diclen) {
      zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
    }
    zf_cell h = zf_pop(ctx);
    zf_cell w = zf_pop(ctx);
    void *buf = (uint8_t *)zf_dump(ctx, &diclen) + (int)addr;

    InitWindow((int)w, (int)h, buf);
    break;
  }
  case ZF_SYSCALL_USER + 5:
    zf_cell fps = zf_pop(ctx);
    SetTargetFPS(fps);
    break;
  case ZF_SYSCALL_USER + 6:
    BeginDrawing();
    break;
  case ZF_SYSCALL_USER + 7:
    EndDrawing();
    break;
  case ZF_SYSCALL_USER + 8: {
    int a = zf_pop(ctx);
    int b = zf_pop(ctx);
    int g = zf_pop(ctx);
    int r = zf_pop(ctx);
    Color bgcolor = CLITERAL(Color){r, g, b, a};
    ClearBackground(bgcolor);
    break;
  }
  case ZF_SYSCALL_USER + 9:
    if (WindowShouldClose())
      zf_push(ctx, -1);
    else
      zf_push(ctx, 0);
    break;
  case ZF_SYSCALL_USER + 10: {
    int a = zf_pop(ctx);
    int b = zf_pop(ctx);
    int g = zf_pop(ctx);
    int r = zf_pop(ctx);

    int size = zf_pop(ctx);
    int y = zf_pop(ctx);
    int x = zf_pop(ctx);

    size_t addr = zf_pop(ctx);
    size_t diclen;
    if (addr >= ZF_DICT_SIZE - diclen) {
      zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
    }
    void *text_to_draw = (uint8_t *)zf_dump(ctx, &diclen) + (size_t)addr;

    DrawText(text_to_draw, x, y, size, (Color){r, g, b, a});
    break;
  }
  default:
    printf("unhandled syscall %d\n", id);
    break;
  }

  return ZF_INPUT_INTERPRET;
}

extern void zf_host_trace(zf_ctx *ctx, const char *fmt, va_list va) {
  fprintf(stderr, "\033[1;30m");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\033[0m");
}

/*
 * Parse number
 */

extern zf_cell zf_host_parse_num(zf_ctx *ctx, const char *buf) {
  zf_cell v;
  int n = 0;
  int r = sscanf(buf, ZF_SCAN_FMT "%n", &v, &n);
  if (r != 1 || buf[n] != '\0') {
    zf_abort(ctx, ZF_ABORT_NOT_A_WORD);
  }
  return v;
}

void input_callback(char *line) {
  if (line) {
    do_eval(ctx, "stdio", ++inputline, line);
    add_history(line);
    free(line);
  }
}

int get_unread_input_count() {
#ifdef _WIN32
  DWORD n;
  return GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &n)
             ? (int)n
             : 0;
#else
  int n;
  return ioctl(fileno(stdin), FIONREAD, &n) ? 0 : n;
#endif
}

int main(void) {
  int r;

  ctx = malloc(sizeof(zf_ctx));
  zf_init(ctx, 0);
  zf_bootstrap(ctx);
  do_eval(ctx, "core", ++inputline, core);
  read_history(".zforth.hist");

  rl_callback_handler_install("Input: ",
                              input_callback); // Install the callback handler

  while (!should_close) {
    // Call rl_callback_read_char to process input
    if (get_unread_input_count() > 0)
      rl_callback_read_char(); // Non-blocking read
    do_eval(ctx, "mainloop", 0, "mainloop");
  }

  rl_callback_handler_remove(); // Clean up when exiting the loop

  write_history(".zforth.hist");
  free(ctx);
  return 0;
}

