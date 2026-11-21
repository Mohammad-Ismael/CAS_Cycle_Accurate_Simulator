#include "vpi_user.h"
#include <stdio.h>
#include <stdlib.h>

static int g_started = 0;
static int g_val_changes = 0;

static int on_start_sim(p_cb_data cb) {
    (void)cb;
    g_started = 1;
    vpi_printf("[VPI] cbStartOfSimulation triggered!\n");
    return 0;
}

static int on_count_change(p_cb_data cb) {
    g_val_changes++;
    s_vpi_value val;
    val.format = vpiIntVal;
    vpi_get_value(cb->obj, &val);
    vpi_printf("[VPI] cbValueChange triggered: count = %d (total: %d)\n", val.value.integer, g_val_changes);
    return 0;
}

static int my_check_calltf(char* user_data) {
    (void)user_data;
    vpi_printf("[VPI] $vpi_check_status invoked: start_sim=%d, val_changes=%d\n", g_started, g_val_changes);
    if (!g_started) {
        vpi_printf("[VPI ERROR] cbStartOfSimulation was not triggered!\n");
        exit(1);
    }
    if (g_val_changes < 2) {
        vpi_printf("[VPI ERROR] Expected at least 2 value changes, got %d!\n", g_val_changes);
        exit(1);
    }
    vpi_printf("[VPI] Status verification successful!\n");
    return 0;
}

void register_my_systf(void) {
    s_vpi_systf_data tf_data;
    tf_data.type = vpiSysTask;
    tf_data.sysfunctype = 0;
    tf_data.tfname = "$vpi_check_status";
    tf_data.calltf = my_check_calltf;
    tf_data.compiletf = NULL;
    tf_data.sizetf = NULL;
    tf_data.user_data = NULL;
    vpi_register_systf(&tf_data);

    // Register cbStartOfSimulation
    s_cb_data cb_start;
    cb_start.reason = cbStartOfSimulation;
    cb_start.cb_rtn = on_start_sim;
    cb_start.obj = NULL;
    cb_start.time = NULL;
    cb_start.value = NULL;
    cb_start.user_data = NULL;
    vpi_register_cb(&cb_start);

    // Register cbValueChange on count
    vpiHandle net_h = vpi_handle_by_name("count", NULL);
    if (net_h) {
        static s_cb_data cb_vc;
        static s_vpi_time t;
        static s_vpi_value v;
        t.type = vpiSimTime;
        v.format = vpiIntVal;
        cb_vc.reason = cbValueChange;
        cb_vc.cb_rtn = on_count_change;
        cb_vc.obj = net_h;
        cb_vc.time = &t;
        cb_vc.value = &v;
        cb_vc.user_data = NULL;
        vpi_register_cb(&cb_vc);
    }
}

void (*vlog_startup_routines[])(void) = {
    register_my_systf,
    NULL
};
