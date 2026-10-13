/******************************************************************************
 * File: basic_compat_test.c
 * Description: Host regression tests for the firmware's classic BASIC parser.
 ******************************************************************************/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "basic_compat.h"
#include "my_basic.h"

static char translated[65536];
static uint32_t line_map[4096];
static const char **answers;
static size_t answer_index;
static size_t steps;

static double seconds(void) { return 42.0; }
static bool sleep_seconds(double delay) { return delay >= 0.0; }
static bool read_key(char *key) { *key = ' '; return true; }

static int graphics_unavailable(struct mb_interpreter_t *s, void **list)
{
    mb_check(mb_attempt_open_bracket(s, list));
    mb_check(mb_attempt_close_bracket(s, list));
    return mb_push_int(s, list, 0);
}

static int input(struct mb_interpreter_t *s, const char *prompt, char *buffer, int size)
{
    (void)s;
    printf("[INPUT %s]\n", prompt);
    assert(answers != NULL && answers[answer_index] != NULL);
    assert(strlen(answers[answer_index]) < (size_t)size);
    strcpy(buffer, answers[answer_index++]);
    return (int)strlen(buffer);
}

static void error(struct mb_interpreter_t *s, mb_error_e code, const char *description,
                  const char *file, int position, unsigned short row,
                  unsigned short column, int abort_code)
{
    (void)s; (void)file; (void)position; (void)abort_code;
    if (code == SE_NO_ERR) return;
    fprintf(stderr, "ERROR %d source line %lu (row %u:%u): %s\n", code,
            (unsigned long)line_map[row ? row - 1 : 0], row, column, description);
}

static int step(struct mb_interpreter_t *s, void **list, const char *file,
                int position, unsigned short row, unsigned short column)
{
    (void)s; (void)list; (void)file; (void)position; (void)row; (void)column;
    return ++steps > 1000000U ? MB_EXTENDED_ABORT : MB_FUNC_OK;
}

static struct mb_interpreter_t *start(const char *source, const char **responses, int *status)
{
    answers = responses;
    answer_index = 0U;
    steps = 0U;
    assert(basic_compat_translate(source, true, translated, sizeof(translated),
                                  line_map, 4096U));
    struct mb_interpreter_t *s = NULL;
    assert(mb_open(&s) == MB_FUNC_OK);
    basic_compat_platform_t platform = {input, seconds, sleep_seconds, read_key};
    assert(basic_compat_register(s, &platform) == MB_FUNC_OK);
    assert(mb_set_error_handler(s, error) == MB_FUNC_OK);
    assert(mb_debug_set_stepped_handler(s, step, NULL) == MB_FUNC_OK);
    *status = mb_load_string(s, translated, true);
    if (*status == MB_FUNC_OK) *status = mb_run(s, true);
    if (*status == MB_FUNC_END) *status = MB_FUNC_OK;
    return s;
}

static struct mb_interpreter_t *run(const char *source, const char **responses)
{
    int status;
    struct mb_interpreter_t *s = start(source, responses, &status);
    if (status != MB_FUNC_OK)
    {
        fprintf(stderr, "STATUS %d after %lu steps\n", status, (unsigned long)steps);
    }
    assert(status == MB_FUNC_OK);
    return s;
}

static void number(struct mb_interpreter_t *s, const char *name, double expected)
{
    mb_value_t value;
    assert(mb_debug_get(s, name, &value) == MB_FUNC_OK);
    double actual = value.type == MB_DT_INT ? (double)value.value.integer : (double)value.value.float_point;
    if (actual != expected) fprintf(stderr, "%s: expected %g got %g\n", name, expected, actual);
    assert(actual == expected);
}

int main(int argc, char **argv)
{
    assert(mb_init() == MB_FUNC_OK);
    struct mb_interpreter_t *s = run(
        "10 DIM A(8,8),B(3)\n"
        "20 DEF FNR(R)=INT(RND(R)*7.98+1.01)\n"
        "30 A(8,8)=17:X=A(8,8):Y=FNR(1):Z=INT(-1.2)\n"
        "40 IF 0 THEN X=99:Y=99\n"
        "50 ON 2 GOTO 70,80\n"
        "60 X=98:GOTO 90\n"
        "70 X=97:GOTO 90\n"
        "80 X=X+1\n"
        "90 END\n", NULL);
    number(s, "X", 18);
    number(s, "Z", -2);
    mb_close(&s);
    s = run("10 X=0:FOR I=3 TO 1 STEP -1\n"
            "20 GOSUB 100:NEXT I\n"
            "30 IF 1 THEN IF 0 THEN X=99 ELSE X=X+1\n"
            "40 ON 0 GOTO 90,100\n50 ON 2 GOSUB 90,100\n60 END\n"
            "90 X=99:RETURN\n100 X=X+I:RETURN\n", NULL);
    number(s, "X", 7);
    mb_close(&s);
    s = run("10 DIM K(3,3)\n20 DEF FND(D)=K(I,1)+S1\n"
            "30 I=2:S1=5:K(2,1)=4:X=FND(0)\n40 END\n", NULL);
    number(s, "X", 9);
    mb_close(&s);
    s = run("10 N=3:M=2:V=7\n"
            "20 X=1:DIM W(N,M),V(INT(N/2)+1,M):Y=2\n"
            "30 W(N,M)=5:V(2,M)=V:X=W(N,M)+V(2,2)+X+Y\n40 END\n", NULL);
    number(s, "X", 15);
    number(s, "V", 7);
    mb_close(&s);
    s = run("10 X=(&HADD8E6):Y=&h0b1222:Z=&HFFFFFF\n"
            "20 A=-&H2A:B=&H10+&h0f:C=&H7FFFFFFF:D=&H80000000\n"
            "30 S$=\"&HADD8E6\":REM &H invalid in a comment\n"
            "40 END\n", NULL);
    number(s, "X", 11393254);
    number(s, "Y", 725538);
    number(s, "Z", 16777215);
    number(s, "A", -42);
    number(s, "B", 31);
    number(s, "C", 2147483647);
    number(s, "D", 2147483648.0);
    mb_value_t literal;
    assert(mb_debug_get(s, "S$", &literal) == MB_FUNC_OK);
    assert(strcmp(literal.value.string, "&HADD8E6") == 0);
    mb_close(&s);
    assert(basic_compat_translate("X=&h2a", false, translated,
                                  sizeof(translated), NULL, 0U));
    assert(strcmp(translated, "X=42\n") == 0);
    assert(basic_compat_translate("X=1 ' &H is a comment", false, translated,
                                  sizeof(translated), NULL, 0U));
    assert(strstr(translated, "' &H is a comment") != NULL);
    assert(basic_compat_translate("X=&HFFFFFFFF", false, translated,
                                  sizeof(translated), NULL, 0U));
    assert(strcmp(translated, "X=4294967295.0\n") == 0);
    const char *invalid_hex[] = {"X=&H", "X=&H)", "X=&HG1", "X=&H12G",
                                "X=&H1_0", "X=&H100000000"};
    for (size_t index = 0U; index < sizeof(invalid_hex) / sizeof(invalid_hex[0]); ++index)
    {
        assert(!basic_compat_translate(invalid_hex[index], false, translated,
                                       sizeof(translated), NULL, 0U));
        assert(basic_compat_error() != NULL);
    }
    assert(!basic_compat_translate("10 DIM A()\n", true, translated,
                                   sizeof(translated), NULL, 0U));
    assert(!basic_compat_translate("10 DIM A(N\n", true, translated,
                                   sizeof(translated), NULL, 0U));
    int status;
    s = start("10 N=-1\n20 DIM A(N)\n30 END\n", NULL, &status);
    assert(status != MB_FUNC_OK);
    mb_close(&s);
    s = start("10 N=100000\n20 DIM A(N,N)\n30 END\n", NULL, &status);
    assert(status != MB_FUNC_OK);
    mb_close(&s);
    char tiny[8];
    puts("#TRACE-BEGIN");
    basic_compat_set_trace(true);
    s = run("10 REM HELLO: WORLD\n"
            "20 X=1:Y=2\n"
            "30 IF X=1 THEN 50\n"
            "40 PRINT \"NO\"\n"
            "50 FOR I=1 TO 2:NEXT I\n"
            "60 DEF FNA(Q)=Q+1\n"
            "70 PRINT \"A\";:Z=FNA(1)\n"
            "80 IF X=2 THEN Y=3:Y=4\n"
            "90 IF X=1 THEN Y=5:GOTO 110\n"
            "100 Y=6\n"
            "110 END\n", NULL);
    basic_compat_set_trace(false);
    puts("#TRACE-END");
    number(s, "Y", 5);
    number(s, "Z", 2);
    mb_close(&s);
    assert(!basic_compat_translate("10 PRINT \"hello\"\n", true, tiny, sizeof(tiny),
                                   NULL, 0U));
    assert(basic_compat_error() != NULL);
    assert(!basic_compat_translate("10 IF 1 PRINT 2\n", true, translated,
                                   sizeof(translated), NULL, 0U));
    s = run("10 GOSUB 100:X=Z:END\n"
            "100 A$=\"   \":B$=\"   \":Z=0\n"
            "110 IF MID$(A$,1,3)<>B$ THEN RETURN\n"
            "120 Z=1:RETURN\n", NULL);
    number(s, "X", 1);
    mb_close(&s);
    const char *responses[] = {"1,2", "hello", NULL};
    s = run("10 INPUT \"COORD\";A,B\n20 INPUT C$\n"
            "30 X=A+B:S$=MID$(C$,2,3)\n40 END\n", responses);
    number(s, "X", 3);
    mb_value_t value;
    assert(mb_debug_get(s, "S$", &value) == MB_FUNC_OK);
    assert(strcmp(value.value.string, "ell") == 0);
    mb_close(&s);
    s = run("10 A$=LEFT$(\"\",3):B$=RIGHT$(\"ABC\",20)\n"
            "20 C$=LEFT$(\"ABC\",0):D$=MID$(\"ABC\",9,2)\n30 END\n", NULL);
    assert(mb_debug_get(s, "B$", &value) == MB_FUNC_OK);
    assert(strcmp(value.value.string, "ABC") == 0);
    assert(mb_debug_get(s, "A$", &value) == MB_FUNC_OK);
    assert(strcmp(value.value.string, "") == 0);
    mb_close(&s);
    const char *invalid_fields[] = {"7,invalid", "7,2,3", "7,1e100"};
    for (size_t index = 0U; index < sizeof(invalid_fields) / sizeof(invalid_fields[0]); ++index)
    {
        s = run("10 A=1:B=2:END\n", NULL);
        const char *invalid_response[] = {invalid_fields[index], NULL};
        answers = invalid_response;
        answer_index = 0U;
        assert(basic_compat_translate("INPUT A,B", false, translated,
                                      sizeof(translated), NULL, 0U));
        assert(mb_load_string(s, translated, true) == MB_FUNC_OK);
        assert(mb_run(s, true) != MB_FUNC_OK);
        number(s, "A", 1);
        number(s, "B", 2);
        mb_close(&s);
    }
    if (argc > 1)
    {
        FILE *file = fopen(argv[1], "rb");
        assert(file != NULL);
        char source[32768];
        size_t size = fread(source, 1U, sizeof(source) - 1U, file);
        source[size] = '\0';
        fclose(file);
        const char *game[] = {"N", "SRS", "LRS", "DAM", "COM", "2", "COM", "4",
                             "1,1", "2,2", "SHE", "500", "NAV", "1", "0.1",
                             "XXX", "NO", NULL};
        s = run(source, game);
        assert(answer_index == 17U);
        mb_close(&s);
        const char *instructions[] = {"Y", "XXX", "NO", NULL};
        s = run(source, instructions);
        assert(answer_index == 3U);
        mb_close(&s);
        static char traced[98304];
        static uint32_t traced_map[4096];
        basic_compat_set_trace(true);
        assert(basic_compat_translate(source, true, traced, sizeof(traced),
                                      traced_map, 4096U));
        basic_compat_set_trace(false);
    }
    if (argc > 2)
    {
        FILE *file = fopen(argv[2], "rb");
        assert(file != NULL);
        char source[32768];
        size_t size = fread(source, 1U, sizeof(source) - 1U, file);
        source[size] = '\0';
        fclose(file);
        const char *maze[] = {"1,1", "12,9", NULL};
        s = run(source, maze);
        assert(answer_index == 2U);
        number(s, "C", 12 * 9 + 1);
        mb_close(&s);
    }
    if (argc > 3)
    {
        FILE *file = fopen(argv[3], "rb");
        assert(file != NULL);
        char source[32768];
        size_t size = fread(source, 1U, sizeof(source) - 1U, file);
        assert(!ferror(file) && feof(file));
        source[size] = '\0';
        fclose(file);
        assert(basic_compat_translate(source, true, translated, sizeof(translated),
                                      line_map, 4096U));
        assert(mb_open(&s) == MB_FUNC_OK);
        basic_compat_platform_t platform = {input, seconds, sleep_seconds, read_key};
        assert(basic_compat_register(s, &platform) == MB_FUNC_OK);
        assert(mb_set_error_handler(s, error) == MB_FUNC_OK);
        const char *functions[] = {
            "GFX_BG_COLOR", "GFX_CHECKED", "GFX_CREATE", "GFX_DELETE", "GFX_FONT",
            "GFX_HIDE", "GFX_OPACITY", "GFX_RADIUS", "GFX_RANGE", "GFX_RGB",
            "GFX_SCREEN", "GFX_SET_POS", "GFX_SET_SIZE", "GFX_SHOW", "GFX_TEXT",
            "GFX_VALUE", "GFX_WIDTH"
        };
        for (size_t index = 0U; index < sizeof(functions) / sizeof(functions[0]); ++index)
        {
            assert(mb_register_func(s, functions[index], graphics_unavailable) != 0);
        }
        assert(mb_load_string(s, translated, true) == MB_FUNC_OK);
        assert(mb_run(s, true) == MB_FUNC_END);
        mb_close(&s);
    }
    assert(mb_dispose() == MB_FUNC_OK);
    puts("Classic BASIC regression tests passed.");
    return 0;
}
