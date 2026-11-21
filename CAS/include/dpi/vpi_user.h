#ifndef VPI_USER_H
#define VPI_USER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

/* Typedefs */
typedef void* vpiHandle;

/* Time types */
#define vpiScaledRealTime 1
#define vpiSimTime        2
#define vpiSuppressTime   3

#ifndef VPI_TIME
#define VPI_TIME
typedef struct t_vpi_time {
    int32_t type;
    uint32_t high;
    uint32_t low;
    double real;
} s_vpi_time, *p_vpi_time;
#endif

/* Logic vector values */
#ifndef VPI_VECVAL
#define VPI_VECVAL
typedef struct t_vpi_vecval {
    uint32_t aval;
    uint32_t bval;
} s_vpi_vecval, *p_vpi_vecval;
#endif

/* Strength values */
typedef struct t_vpi_strengthval {
    int32_t logic;
    int32_t s0;
    int32_t s1;
} s_vpi_strengthval, *p_vpi_strengthval;

/* Value formats */
#define vpiBinStrVal          1
#define vpiOctStrVal          2
#define vpiDecStrVal          3
#define vpiHexStrVal          4
#define vpiScalarVal          5
#define vpiIntVal             6
#define vpiRealVal            7
#define vpiStringVal          8
#define vpiVectorVal          9
#define vpiStrengthVal        10
#define vpiTimeVal            11
#define vpiObjTypeVal         12
#define vpiSuppressVal        13
#define vpiShortIntVal        14
#define vpiLongIntVal         15
#define vpiShortRealVal       16
#define vpiRawTwoStateVal     17
#define vpiRawFourStateVal    18

/* Scalar values */
#define vpi0 0
#define vpi1 1
#define vpiZ 2
#define vpiX 3
#define vpiH 4
#define vpiL 5
#define vpiDontCare 6

typedef struct t_vpi_value {
    int32_t format;
    union {
        char *str;
        int32_t scalar;
        int32_t integer;
        double real;
        struct t_vpi_time *time;
        struct t_vpi_vecval *vector;
        struct t_vpi_strengthval *strength;
        char *misc;
    } value;
} s_vpi_value, *p_vpi_value;

/* Object types */
#define vpiModule             32
#define vpiNet                36
#define vpiReg                48
#define vpiPort               44
#define vpiTask               59
#define vpiFunction           20
#define vpiParameter          41
#define vpiConstant           11
#define vpiVariables          65
#define vpiIntegerVar         25
#define vpiRealVar            47
#define vpiTimeVar            63
#define vpiBitVar             80
#define vpiByteVar            81
#define vpiShortIntVar        82
#define vpiIntVar             83
#define vpiLongIntVar         84
#define vpiStringVar          88
#define vpiArrayVar           89
#define vpiMemory             40
#define vpiIterator           1

/* Object properties */
#define vpiType               1
#define vpiName               2
#define vpiFullName           3
#define vpiSize               4
#define vpiFile               5
#define vpiLineNo             6
#define vpiTopModule          7
#define vpiCell               8
#define vpiSigned             9
#define vpiStop               0
#define vpiFinish             1
#define vpiReset              2

#define vpiTimeUnit           11
#define vpiTimePrecision      12

/* Callback reasons */
#define cbValueChange         1
#define cbStatement           2
#define cbTimed               3
#define cbCreateObj           4
#define cbRemoveObj           5
#define cbStartOfSimulation   11
#define cbEndOfSimulation     12
#define cbAfterDelay          17
#define cbReadWriteSynch      18
#define cbReadOnlySynch       19
#define cbNextSimTime         20
#define cbEnterInteractive    21
#define cbExitInteractive     22
#define cbInteractiveScopeChange 23
#define cbUnresolvedSystf     24

typedef struct t_cb_data {
    int32_t reason;
    int32_t (*cb_rtn)(struct t_cb_data *);
    vpiHandle obj;
    p_vpi_time time;
    p_vpi_value value;
    int32_t index;
    char *user_data;
} s_cb_data, *p_cb_data;

/* System task / function registration */
#define vpiSysTask            1
#define vpiSysFunc            2

#define vpiIntFunc            1
#define vpiRealFunc           2
#define vpiTimeFunc           3
#define vpiSizedFunc          4
#define vpiSizedSignedFunc    5

typedef struct t_vpi_systf_data {
    int32_t type;
    int32_t sysfunctype;
    char *tfname;
    int32_t (*calltf)(char *);
    int32_t (*compiletf)(char *);
    int32_t (*sizetf)(char *);
    char *user_data;
} s_vpi_systf_data, *p_vpi_systf_data;

/* Put value flags */
#define vpiNoDelay            1
#define vpiInertialDelay      2
#define vpiTransportDelay     3
#define vpiPureTransportDelay 4

/* Function prototypes */
vpiHandle vpi_register_cb(p_cb_data cb_data_p);
int32_t vpi_remove_cb(vpiHandle cb_obj);
vpiHandle vpi_register_systf(p_vpi_systf_data systf_data_p);
vpiHandle vpi_handle_by_name(char *name, vpiHandle scope);
vpiHandle vpi_handle(int32_t type, vpiHandle refHandle);
vpiHandle vpi_iterate(int32_t type, vpiHandle refHandle);
vpiHandle vpi_scan(vpiHandle iterator);
int32_t vpi_get(int32_t property, vpiHandle object);
char *vpi_get_str(int32_t property, vpiHandle object);
void vpi_get_value(vpiHandle expr, p_vpi_value value_p);
vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p, int32_t flags);
void vpi_get_time(vpiHandle object, p_vpi_time time_p);
int32_t vpi_printf(const char *format, ...);
int32_t vpi_vprintf(const char *format, va_list ap);
int32_t vpi_control(int32_t operation, ...);
int32_t vpi_free_object(vpiHandle object);

/* Startup routines table */
extern void (*vlog_startup_routines[])(void);

#ifdef __cplusplus
}
#endif

#endif /* VPI_USER_H */
