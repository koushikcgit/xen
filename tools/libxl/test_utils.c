#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include "test_common.h"
#include "test_utils.h"
#include "libxl_osdeps.h"
#include "libxl.h"
#include "libxl_utils.h"
#include "libxlutil.h"


char *default_vifscript = NULL;
char *default_bridge = NULL;
char *default_gatewaydev = NULL;
char *default_vifbackend = NULL;
char *blkdev_start;
int run_hotplug_scripts = 1;
int claim_mode = 1;
int autoballoon = -1; 


static const char *action_on_shutdown_names[] = {
    [LIBXL_ACTION_ON_SHUTDOWN_DESTROY] = "destroy",

    [LIBXL_ACTION_ON_SHUTDOWN_RESTART] = "restart",
    [LIBXL_ACTION_ON_SHUTDOWN_RESTART_RENAME] = "rename-restart",

    [LIBXL_ACTION_ON_SHUTDOWN_PRESERVE] = "preserve",

    [LIBXL_ACTION_ON_SHUTDOWN_COREDUMP_DESTROY] = "coredump-destroy",
    [LIBXL_ACTION_ON_SHUTDOWN_COREDUMP_RESTART] = "coredump-restart",
};


#define ARRAY_EXTEND_INIT(array,count,initfn)                           \
    ({                                                                  \
        typeof((count)) array_extend_old_count = (count);               \
        (count)++;                                                      \
        (array) = xrealloc((array), sizeof(*array) * (count));          \
        (initfn)(&(array)[array_extend_old_count]);                     \
        (array)[array_extend_old_count].devid = array_extend_old_count; \
        &(array)[array_extend_old_count];                               \
    })

static void *xrealloc(void *ptr, size_t sz) {
    void *r;
    if (!sz) { free(ptr); return 0; }
      /* realloc(non-0, 0) has a useless return value;
       * but xrealloc(anything, 0) is like free
       */
    r = realloc(ptr, sz);
    if (!r) { fprintf(stderr,"xl: Unable to realloc to %lu bytes.\n",
                      (unsigned long)sz); exit(-ERROR_FAIL); }
    return r;
}

#if 0
static void *xmalloc(size_t sz) {
    void *r;
    r = malloc(sz);
    if (!r) { fprintf(stderr,"xl: Unable to malloc %lu bytes.\n",
                      (unsigned long)sz); exit(-ERROR_FAIL); }
    return r;
}
#endif

static char *xstrdup(const char *x)
{
    char *r;
    r = strdup(x);
    if (!r) {
        fprintf(stderr, "xl: Unable to strdup a string of length %zu.\n",
                strlen(x));
        exit(-ERROR_FAIL);
    }
    return r;
}


static void set_default_nic_values(libxl_device_nic *nic)
{

    if (default_vifscript) {
        free(nic->script);
        nic->script = strdup(default_vifscript);
    }

    if (default_bridge) {
        free(nic->bridge);
        nic->bridge = strdup(default_bridge);
    }

    if (default_gatewaydev) {
        free(nic->gatewaydev);
        nic->gatewaydev = strdup(default_gatewaydev);
    }

    if (default_vifbackend) {
        free(nic->backend_domname);
        nic->backend_domname = strdup(default_vifbackend);
    }
}


static void parse_vif_rate(XLU_Config **config, const char *rate,
                           libxl_device_nic *nic)
{
    int e;

    e = xlu_vif_parse_rate(*config, rate, nic);
    if (e == EINVAL || e == EOVERFLOW) exit(-1);
    if (e) {
        fprintf(stderr,"xlu_vif_parse_rate failed: %s\n",strerror(errno));
        exit(-1);
    }
}

static void parse_top_level_vnc_options(XLU_Config *config,
                                        libxl_vnc_info *vnc)
{
    long l;

    xlu_cfg_get_defbool(config, "vnc", &vnc->enable, 0);
    xlu_cfg_replace_string (config, "vnclisten", &vnc->listen, 0);
    xlu_cfg_replace_string (config, "vncpasswd", &vnc->passwd, 0);
    if (!xlu_cfg_get_long (config, "vncdisplay", &l, 0))
        vnc->display = l;
    xlu_cfg_get_defbool(config, "vncunused", &vnc->findunused, 0);
}

static void parse_top_level_sdl_options(XLU_Config *config,
                                        libxl_sdl_info *sdl)
{
    xlu_cfg_get_defbool(config, "sdl", &sdl->enable, 0);
    xlu_cfg_get_defbool(config, "opengl", &sdl->opengl, 0);
    xlu_cfg_replace_string (config, "display", &sdl->display, 0);
    xlu_cfg_replace_string (config, "xauthority", &sdl->xauthority, 0);
}

static void split_string_into_string_list(const char *str,
                                          const char *delim,
                                          libxl_string_list *psl)
{
    char *s, *saveptr;
    const char *p;
    libxl_string_list sl;

    int i = 0, nr = 0;

    s = strdup(str);
    if (s == NULL) {
        fprintf(stderr, "unable to allocate memory to split string\n");
        exit(-1);
    }

    /* Count number of entries */
    p = strtok_r(s, delim, &saveptr);
    do {
        nr++;
    } while ((p = strtok_r(NULL, delim, &saveptr)));

    free(s);

    s = strdup(str);

    sl = malloc((nr+1) * sizeof (char *));
    if (sl == NULL) {
        fprintf(stderr, "unable to allocate memory to split string\n");
        exit(-1);
    }

    p = strtok_r(s, delim, &saveptr);
    do {
        assert(i < nr);
        sl[i] = strdup(p);
        i++;
    } while ((p = strtok_r(NULL, delim, &saveptr)));
    sl[i] = NULL;

    *psl = sl;

    free(s);
}

static void replace_string(char **str, const char *val)
{
    free(*str);
    *str = xstrdup(val);
}

static int match_option_size(const char *prefix, size_t len,
        char *arg, char **argopt)
{
    int rc = strncmp(prefix, arg, len);
    if (!rc) *argopt = arg+len;
    return !rc;
}
#define MATCH_OPTION(prefix, arg, oparg) \
    match_option_size((prefix "="), sizeof((prefix)), (arg), &(oparg))

/* Parses network data and adds info into nic
 * Returns 1 if the input token does not match one of the keys
 * or parsed values are not correct. Successful parse returns 0 */
static int parse_nic_config(libxl_device_nic *nic, XLU_Config **config, char *token)
{
    char *endptr, *oparg;
    int i;
    unsigned int val;

    if (MATCH_OPTION("type", token, oparg)) {
        if (!strcmp("vif", oparg)) {
            nic->nictype = LIBXL_NIC_TYPE_VIF;
        } else if (!strcmp("ioemu", oparg)) {
            nic->nictype = LIBXL_NIC_TYPE_VIF_IOEMU;
        } else {
            fprintf(stderr, "Invalid parameter `type'.\n");
            return 1;
        }
    } else if (MATCH_OPTION("mac", token, oparg)) {
        for (i = 0; i < 6; i++) {
            val = strtoul(oparg, &endptr, 16);
            if ((oparg == endptr) || (val > 255)) {
                fprintf(stderr, "Invalid parameter `mac'.\n");
                return 1;
            }
            nic->mac[i] = val;
            oparg = endptr + 1;
        }
    } else if (MATCH_OPTION("bridge", token, oparg)) {
        replace_string(&nic->bridge, oparg);
    } else if (MATCH_OPTION("netdev", token, oparg)) {
        fprintf(stderr, "the netdev parameter is deprecated, "
                        "please use gatewaydev instead\n");
        replace_string(&nic->gatewaydev, oparg);
    } else if (MATCH_OPTION("gatewaydev", token, oparg)) {
        replace_string(&nic->gatewaydev, oparg);
    } else if (MATCH_OPTION("ip", token, oparg)) {
        replace_string(&nic->ip, oparg);
    } else if (MATCH_OPTION("script", token, oparg)) {
        replace_string(&nic->script, oparg);
    } else if (MATCH_OPTION("backend", token, oparg)) {
        replace_string(&nic->backend_domname, oparg);
    } else if (MATCH_OPTION("vifname", token, oparg)) {
        replace_string(&nic->ifname, oparg);
    } else if (MATCH_OPTION("model", token, oparg)) {
        replace_string(&nic->model, oparg);
    } else if (MATCH_OPTION("rate", token, oparg)) {
        parse_vif_rate(config, oparg, nic);
    } else if (MATCH_OPTION("accel", token, oparg)) {
        fprintf(stderr, "the accel parameter for vifs is currently not supported\n");
    } else {
        fprintf(stderr, "unrecognized argument `%s'\n", token);
        return 1;
    }
    return 0;
}


static int parse_action_on_shutdown(const char *buf, libxl_action_on_shutdown *a)
{
    int i;
    const char *n;

    for (i = 0; i < sizeof(action_on_shutdown_names) / sizeof(action_on_shutdown_names[0]); i++) {
        n = action_on_shutdown_names[i];

        if (!n) continue;

        if (strcmp(buf, n) == 0) {
            *a = i;
            return 1;
        }
    }
    return 0;
}


static char *parse_cmdline(XLU_Config *config)
{
    char *cmdline = NULL;
    const char *root = NULL, *extra = NULL, *buf = NULL;

    xlu_cfg_get_string (config, "cmdline", &buf, 0);
    xlu_cfg_get_string (config, "root", &root, 0);
    xlu_cfg_get_string (config, "extra", &extra, 0);

    if (buf) {
        cmdline = strdup(buf);
        if (root || extra)
            fprintf(stderr, "Warning: ignoring root= and extra= "
                    "in favour of cmdline=\n");
    } else {
        if (root && extra) {
            cmdline = (char*)malloc((strlen(root)+ strlen(extra)+ 256) * sizeof(char));
            if (sprintf(cmdline, "root=%s %s", root, extra) == -1){
                free(cmdline);
                cmdline = NULL;
            }
        } else if (root) {
            cmdline = (char*)malloc((strlen(root)+ 256) * sizeof(char));
            if (sprintf(cmdline, "root=%s", root) == -1){
                free(cmdline);
                cmdline = NULL;
            }
        } else if (extra) {
            cmdline = strdup(extra);
        }
    }

    if ((buf || root || extra) && !cmdline) {
        fprintf(stderr, "Failed to allocate memory for cmdline\n");
        exit(1);
    }

    return cmdline;
}

static void parse_disk_config_multistring(XLU_Config **config,
                                          int nspecs, const char *const *specs,
                                          libxl_device_disk *disk)
{
    int e;

    libxl_device_disk_init(disk);

    if (!*config) {
        *config = xlu_cfg_init(stderr, "command line");
        if (!*config) { perror("xlu_cfg_init"); exit(-1); }
    }

    e = xlu_disk_parse(*config, nspecs, specs, disk);
    if (e == EINVAL) exit(-1);
    if (e) {
        fprintf(stderr,"xlu_disk_parse failed: %s\n",strerror(errno));
        exit(-1);
    }
}

static void parse_disk_config(XLU_Config **config, const char *spec,
                              libxl_device_disk *disk)
{
    parse_disk_config_multistring(config, 1, &spec, disk);
}



void parse_config_data(const char *config_source,
                              const char *config_data,
                              int config_len,
                              libxl_domain_config *d_config)
{
    const char *buf;
    long l;
    XLU_Config *config;
    XLU_ConfigList *vbds, *nics, *pcis, *cpuids, *vtpms;
    XLU_ConfigList *ioports, *irqs, *viridian;
    int num_ioports, num_irqs, num_viridian;
    int pci_power_mgmt = 0;
    int pci_msitranslate = 0;
    int pci_permissive = 0;
    int pci_seize = 0;
    int i, e;
    char *kernel_basename;

    libxl_domain_create_info *c_info = &d_config->c_info;
    libxl_domain_build_info *b_info = &d_config->b_info;

    config= xlu_cfg_init(stderr, config_source);
    if (!config) {
        fprintf(stderr, "Failed to allocate for configuration\n");
        exit(1);
    }

    e= xlu_cfg_readdata(config, config_data, config_len);
    if (e) {
        fprintf(stderr, "Failed to parse config: %s\n", strerror(e));
        exit(1);
    }

    if (!xlu_cfg_get_string (config, "init_seclabel", &buf, 0))
        xlu_cfg_replace_string(config, "init_seclabel",
                               &c_info->ssid_label, 0);

    if (!xlu_cfg_get_string (config, "seclabel", &buf, 0)) {
        if (c_info->ssid_label)
            xlu_cfg_replace_string(config, "seclabel",
                                   &b_info->exec_ssid_label, 0);
        else
            xlu_cfg_replace_string(config, "seclabel",
                                   &c_info->ssid_label, 0);
    }

    libxl_defbool_set(&c_info->run_hotplug_scripts, run_hotplug_scripts);
    c_info->type = LIBXL_DOMAIN_TYPE_PV;
    if (!xlu_cfg_get_string (config, "builder", &buf, 0) &&
        !strncmp(buf, "hvm", strlen(buf)))
        c_info->type = LIBXL_DOMAIN_TYPE_HVM;

    xlu_cfg_get_defbool(config, "pvh", &c_info->pvh, 0);
    xlu_cfg_get_defbool(config, "hap", &c_info->hap, 0);

    if (xlu_cfg_replace_string (config, "name", &c_info->name, 0)) {
        fprintf(stderr, "Domain name must be specified.\n");
        exit(1);
    }

    if (!xlu_cfg_get_string (config, "uuid", &buf, 0) ) {
        if ( libxl_uuid_from_string(&c_info->uuid, buf) ) {
            fprintf(stderr, "Failed to parse UUID: %s\n", buf);
            exit(1);
        }
    }else{
        libxl_uuid_generate(&c_info->uuid);
    }

    xlu_cfg_get_defbool(config, "oos", &c_info->oos, 0);

    if (!xlu_cfg_get_string (config, "pool", &buf, 0))
        xlu_cfg_replace_string(config, "pool", &c_info->pool_name, 0);

    libxl_domain_build_info_init_type(b_info, c_info->type);
    if (blkdev_start)
        b_info->blkdev_start = strdup(blkdev_start);

    /* the following is the actual config parsing with overriding
     * values in the structures */
    if (!xlu_cfg_get_long (config, "cpu_weight", &l, 0))
        b_info->sched_params.weight = l;
    if (!xlu_cfg_get_long (config, "cap", &l, 0))
        b_info->sched_params.cap = l;
    if (!xlu_cfg_get_long (config, "period", &l, 0))
        b_info->sched_params.period = l;
    if (!xlu_cfg_get_long (config, "slice", &l, 0))
        b_info->sched_params.slice = l;
    if (!xlu_cfg_get_long (config, "latency", &l, 0))
        b_info->sched_params.latency = l;
    if (!xlu_cfg_get_long (config, "extratime", &l, 0))
        b_info->sched_params.extratime = l;

    if (!xlu_cfg_get_long (config, "vcpus", &l, 0)) {
        b_info->max_vcpus = l;

        if (libxl_cpu_bitmap_alloc(ctx, &b_info->avail_vcpus, l)) {
            fprintf(stderr, "Unable to allocate cpumap\n");
            exit(1);
        }
        libxl_bitmap_set_none(&b_info->avail_vcpus);
        while (l-- > 0)
            libxl_bitmap_set((&b_info->avail_vcpus), l);
    }

    if (!xlu_cfg_get_long (config, "maxvcpus", &l, 0))
        b_info->max_vcpus = l;

    buf = NULL;
#if 0
    if (!xlu_cfg_get_list (config, "cpus", &cpus, &num_cpus, 1) ||
        !xlu_cfg_get_string (config, "cpus", &buf, 0))
        parse_vcpu_affinity(b_info, cpus, buf, num_cpus, /* is_hard */ true);

    buf = NULL;
    if (!xlu_cfg_get_list (config, "cpus_soft", &cpus, &num_cpus, 1) ||
        !xlu_cfg_get_string (config, "cpus_soft", &buf, 0))
        parse_vcpu_affinity(b_info, cpus, buf, num_cpus, false);
#endif
    if (!xlu_cfg_get_long (config, "memory", &l, 0)) {
        b_info->max_memkb = l * 1024;
        b_info->target_memkb = b_info->max_memkb;
    }

    if (!xlu_cfg_get_long (config, "maxmem", &l, 0))
        b_info->max_memkb = l * 1024;

    libxl_defbool_set(&b_info->claim_mode, claim_mode);

    if (xlu_cfg_get_string (config, "on_poweroff", &buf, 0))
        buf = "destroy";
    if (!parse_action_on_shutdown(buf, &d_config->on_poweroff)) {
        fprintf(stderr, "Unknown on_poweroff action \"%s\" specified\n", buf);
        exit(1);
    }

    if (xlu_cfg_get_string (config, "on_reboot", &buf, 0))
        buf = "restart";
    if (!parse_action_on_shutdown(buf, &d_config->on_reboot)) {
        fprintf(stderr, "Unknown on_reboot action \"%s\" specified\n", buf);
        exit(1);
    }

    if (xlu_cfg_get_string (config, "on_watchdog", &buf, 0))
        buf = "destroy";
    if (!parse_action_on_shutdown(buf, &d_config->on_watchdog)) {
        fprintf(stderr, "Unknown on_watchdog action \"%s\" specified\n", buf);
        exit(1);
    }


    if (xlu_cfg_get_string (config, "on_crash", &buf, 0))
        buf = "destroy";
    if (!parse_action_on_shutdown(buf, &d_config->on_crash)) {
        fprintf(stderr, "Unknown on_crash action \"%s\" specified\n", buf);
        exit(1);
    }

    /* libxl_get_required_shadow_memory() must be called after final values
     * (default or specified) for vcpus and memory are set, because the
     * calculation depends on those values. */
    b_info->shadow_memkb = !xlu_cfg_get_long(config, "shadow_memory", &l, 0)
        ? l * 1024
        : libxl_get_required_shadow_memory(b_info->max_memkb,
                                           b_info->max_vcpus);

    xlu_cfg_get_defbool(config, "nomigrate", &b_info->disable_migrate, 0);

    if (!xlu_cfg_get_long(config, "tsc_mode", &l, 1)) {
        const char *s = libxl_tsc_mode_to_string(l);
        fprintf(stderr, "WARNING: specifying \"tsc_mode\" as an integer is deprecated. "
                "Please use the named parameter variant. %s%s%s\n",
                s ? "e.g. tsc_mode=\"" : "",
                s ? s : "",
                s ? "\"" : "");

        if (l < LIBXL_TSC_MODE_DEFAULT ||
            l > LIBXL_TSC_MODE_NATIVE_PARAVIRT) {
            fprintf(stderr, "ERROR: invalid value %ld for \"tsc_mode\"\n", l);
            exit (1);
        }
        b_info->tsc_mode = l;
    } else if (!xlu_cfg_get_string(config, "tsc_mode", &buf, 0)) {
        fprintf(stderr, "got a tsc mode string: \"%s\"\n", buf);
        if (libxl_tsc_mode_from_string(buf, &b_info->tsc_mode)) {
            fprintf(stderr, "ERROR: invalid value \"%s\" for \"tsc_mode\"\n",
                    buf);
            exit (1);
        }
    }

    if (!xlu_cfg_get_long(config, "rtc_timeoffset", &l, 0))
        b_info->rtc_timeoffset = l;

    if (!xlu_cfg_get_long(config, "vncviewer", &l, 0))
        fprintf(stderr, "WARNING: ignoring \"vncviewer\" option. "
                "Use \"-V\" option of \"xl create\" to automatically spawn vncviewer.\n");

    xlu_cfg_get_defbool(config, "localtime", &b_info->localtime, 0);

    if (!xlu_cfg_get_long (config, "videoram", &l, 0))
        b_info->video_memkb = l * 1024;

    if (!xlu_cfg_get_long(config, "max_event_channels", &l, 0))
        b_info->event_channels = l;

    xlu_cfg_replace_string (config, "kernel", &b_info->kernel, 0);
    xlu_cfg_replace_string (config, "ramdisk", &b_info->ramdisk, 0);
    b_info->cmdline = parse_cmdline(config);

    xlu_cfg_get_defbool(config, "driver_domain", &c_info->driver_domain, 0);

    switch(b_info->type) {
    case LIBXL_DOMAIN_TYPE_HVM:
        kernel_basename = libxl_basename(b_info->kernel);
        if (!strcmp(kernel_basename, "hvmloader")) {
            fprintf(stderr, "WARNING: you seem to be using \"kernel\" "
                    "directive to override HVM guest firmware. Ignore "
                    "that. Use \"firmware_override\" instead if you "
                    "really want a non-default firmware\n");
            b_info->kernel = NULL;
        }
        free(kernel_basename);

        xlu_cfg_replace_string (config, "firmware_override",
                                &b_info->u.hvm.firmware, 0);
        if (!xlu_cfg_get_string(config, "bios", &buf, 0) &&
            libxl_bios_type_from_string(buf, &b_info->u.hvm.bios)) {
                fprintf(stderr, "ERROR: invalid value \"%s\" for \"bios\"\n",
                    buf);
                exit (1);
        }

        xlu_cfg_get_defbool(config, "pae", &b_info->u.hvm.pae, 0);
        xlu_cfg_get_defbool(config, "apic", &b_info->u.hvm.apic, 0);
        xlu_cfg_get_defbool(config, "acpi", &b_info->u.hvm.acpi, 0);
        xlu_cfg_get_defbool(config, "acpi_s3", &b_info->u.hvm.acpi_s3, 0);
        xlu_cfg_get_defbool(config, "acpi_s4", &b_info->u.hvm.acpi_s4, 0);
        xlu_cfg_get_defbool(config, "nx", &b_info->u.hvm.nx, 0);
        xlu_cfg_get_defbool(config, "hpet", &b_info->u.hvm.hpet, 0);
        xlu_cfg_get_defbool(config, "vpt_align", &b_info->u.hvm.vpt_align, 0);

        switch (xlu_cfg_get_list(config, "viridian",
                                 &viridian, &num_viridian, 1))
        {
        case 0: /* Success */
            if (num_viridian) {
                libxl_bitmap_alloc(ctx, &b_info->u.hvm.viridian_enable,
                                   LIBXL_BUILDINFO_HVM_VIRIDIAN_ENABLE_DISABLE_WIDTH);
                libxl_bitmap_alloc(ctx, &b_info->u.hvm.viridian_disable,
                                   LIBXL_BUILDINFO_HVM_VIRIDIAN_ENABLE_DISABLE_WIDTH);
            }
            for (i = 0; i < num_viridian; i++) {
                libxl_viridian_enlightenment v;

                buf = xlu_cfg_get_listitem(viridian, i);
                if (strcmp(buf, "all") == 0)
                    libxl_bitmap_set_any(&b_info->u.hvm.viridian_enable);
                else if (strcmp(buf, "defaults") == 0)
                    libxl_defbool_set(&b_info->u.hvm.viridian, true);
                else {
                    libxl_bitmap *s = &b_info->u.hvm.viridian_enable;
                    libxl_bitmap *r = &b_info->u.hvm.viridian_disable;

                    if (*buf == '!') {
                        s = &b_info->u.hvm.viridian_disable;
                        r = &b_info->u.hvm.viridian_enable;
                        buf++;
                    }

                    e = libxl_viridian_enlightenment_from_string(buf, &v);
                    if (e) {
                        fprintf(stderr,
                                "xl: unknown viridian enlightenment '%s'\n",
                                buf);
                        exit(-ERROR_FAIL);
                    }

                    libxl_bitmap_set(s, v);
                    libxl_bitmap_reset(r, v);
                }
            }
            break;
        case ESRCH: break; /* Option not present */
        case EINVAL:
            xlu_cfg_get_defbool(config, "viridian", &b_info->u.hvm.viridian, 1);
            break;
        default:
            fprintf(stderr,"xl: Unable to parse viridian enlightenments.\n");
            exit(-ERROR_FAIL);
        }
#if 0
        if (!xlu_cfg_get_long(config, "mmio_hole", &l, 0)) {
            uint64_t mmio_hole_size;

            b_info->u.hvm.mmio_hole_memkb = l * 1024;
            mmio_hole_size = b_info->u.hvm.mmio_hole_memkb * 1024;
            if (mmio_hole_size < HVM_BELOW_4G_MMIO_LENGTH ||
                mmio_hole_size > HVM_BELOW_4G_MMIO_START) {
                fprintf(stderr,
                        "ERROR: invalid value %ld for \"mmio_hole\"\n", l);
                exit (1);
            }
        }
#endif
        if (!xlu_cfg_get_long(config, "timer_mode", &l, 1)) {
            const char *s = libxl_timer_mode_to_string(l);
            fprintf(stderr, "WARNING: specifying \"timer_mode\" as an integer is deprecated. "
                    "Please use the named parameter variant. %s%s%s\n",
                    s ? "e.g. timer_mode=\"" : "",
                    s ? s : "",
                    s ? "\"" : "");

            if (l < LIBXL_TIMER_MODE_DELAY_FOR_MISSED_TICKS ||
                l > LIBXL_TIMER_MODE_ONE_MISSED_TICK_PENDING) {
                fprintf(stderr, "ERROR: invalid value %ld for \"timer_mode\"\n", l);
                exit (1);
            }
            b_info->u.hvm.timer_mode = l;
        } else if (!xlu_cfg_get_string(config, "timer_mode", &buf, 0)) {
            if (libxl_timer_mode_from_string(buf, &b_info->u.hvm.timer_mode)) {
                fprintf(stderr, "ERROR: invalid value \"%s\" for \"timer_mode\"\n",
                        buf);
                exit (1);
            }
        }

        xlu_cfg_get_defbool(config, "nestedhvm", &b_info->u.hvm.nested_hvm, 0);

        xlu_cfg_replace_string(config, "smbios_firmware",
                               &b_info->u.hvm.smbios_firmware, 0);
        xlu_cfg_replace_string(config, "acpi_firmware",
                               &b_info->u.hvm.acpi_firmware, 0);

        if (!xlu_cfg_get_string(config, "ms_vm_genid", &buf, 0)) {
            if (!strcmp(buf, "generate")) {
                e = libxl_ms_vm_genid_generate(ctx, &b_info->u.hvm.ms_vm_genid);
                if (e) {
                    fprintf(stderr, "ERROR: failed to generate a VM Generation ID\n");
                    exit(1);
                }
            } else if (!strcmp(buf, "none")) {
                ;
            } else {
                    fprintf(stderr, "ERROR: \"ms_vm_genid\" option must be \"generate\" or \"none\"\n");
                    exit(1);
            }
        }
        break;
    case LIBXL_DOMAIN_TYPE_PV:
    {
        xlu_cfg_replace_string (config, "bootloader", &b_info->u.pv.bootloader, 0);
        switch (xlu_cfg_get_list_as_string_list(config, "bootloader_args",
                                      &b_info->u.pv.bootloader_args, 1))
        {

        case 0: break; /* Success */
        case ESRCH: break; /* Option not present */
        case EINVAL:
            if (!xlu_cfg_get_string(config, "bootloader_args", &buf, 0)) {

                fprintf(stderr, "WARNING: Specifying \"bootloader_args\""
                        " as a string is deprecated. "
                        "Please use a list of arguments.\n");
                split_string_into_string_list(buf, " \t\n",
                                              &b_info->u.pv.bootloader_args);
            }
            break;
        default:
            fprintf(stderr,"xl: Unable to parse bootloader_args.\n");
            exit(-ERROR_FAIL);
        }

        if (!b_info->u.pv.bootloader && !b_info->kernel) {
            fprintf(stderr, "Neither kernel nor bootloader specified\n");
            exit(1);
        }

        break;
    }
    default:
        abort();
    }

    if (!xlu_cfg_get_list(config, "ioports", &ioports, &num_ioports, 0)) {
        b_info->num_ioports = num_ioports;
        b_info->ioports = calloc(num_ioports, sizeof(*b_info->ioports));
        if (b_info->ioports == NULL) {
            fprintf(stderr, "unable to allocate memory for ioports\n");
            exit(-1);
        }

        for (i = 0; i < num_ioports; i++) {
            const char *buf2;
            char *ep;
            uint32_t start, end;
            unsigned long ul;

            buf = xlu_cfg_get_listitem (ioports, i);
            if (!buf) {
                fprintf(stderr,
                        "xl: Unable to get element #%d in ioport list\n", i);
                exit(1);
            }
            ul = strtoul(buf, &ep, 16);
            if (ep == buf) {
                fprintf(stderr, "xl: Invalid argument parsing ioport: %s\n",
                        buf);
                exit(1);
            }
            if (ul >= UINT32_MAX) {
                fprintf(stderr, "xl: ioport %lx too big\n", ul);
                exit(1);
            }
            start = end = ul;

            if (*ep == '-') {
                buf2 = ep + 1;
                ul = strtoul(buf2, &ep, 16);
                if (ep == buf2 || *ep != '\0' || start > end) {
                    fprintf(stderr,
                            "xl: Invalid argument parsing ioport: %s\n", buf);
                    exit(1);
                }
                if (ul >= UINT32_MAX) {
                    fprintf(stderr, "xl: ioport %lx too big\n", ul);
                    exit(1);
                }
                end = ul;
            } else if ( *ep != '\0' )
                fprintf(stderr,
                        "xl: Invalid argument parsing ioport: %s\n", buf);
            b_info->ioports[i].first = start;
            b_info->ioports[i].number = end - start + 1;
        }
    }

    if (!xlu_cfg_get_list(config, "irqs", &irqs, &num_irqs, 0)) {
        b_info->num_irqs = num_irqs;
        b_info->irqs = calloc(num_irqs, sizeof(*b_info->irqs));
        if (b_info->irqs == NULL) {
            fprintf(stderr, "unable to allocate memory for ioports\n");
            exit(-1);
        }
        for (i = 0; i < num_irqs; i++) {
            char *ep;
            unsigned long ul;
            buf = xlu_cfg_get_listitem (irqs, i);
            if (!buf) {
                fprintf(stderr,
                        "xl: Unable to get element %d in irq list\n", i);
                exit(1);
            }
            ul = strtoul(buf, &ep, 10);
            if (ep == buf) {
                fprintf(stderr,
                        "xl: Invalid argument parsing irq: %s\n", buf);
                exit(1);
            }
            if (ul >= UINT32_MAX) {
                fprintf(stderr, "xl: irq %lx too big\n", ul);
                exit(1);
            }
            b_info->irqs[i] = ul;
        }
    }
#if 0
    if (!xlu_cfg_get_list(config, "iomem", &iomem, &num_iomem, 0)) {
        int ret;
        b_info->num_iomem = num_iomem;
        b_info->iomem = calloc(num_iomem, sizeof(*b_info->iomem));
        if (b_info->iomem == NULL) {
            fprintf(stderr, "unable to allocate memory for iomem\n");
            exit(-1);
        }
        for (i = 0; i < num_iomem; i++) {
            buf = xlu_cfg_get_listitem (iomem, i);
            if (!buf) {
                fprintf(stderr,
                        "xl: Unable to get element %d in iomem list\n", i);
                exit(1);
            }
            libxl_iomem_range_init(&b_info->iomem[i]);
            ret = sscanf(buf, "%" SCNx64",%" SCNx64"@%" SCNx64,
                         &b_info->iomem[i].start,
                         &b_info->iomem[i].number,
                         &b_info->iomem[i].gfn);
            if (ret < 2) {
                fprintf(stderr,
                        "xl: Invalid argument parsing iomem: %s\n", buf);
                exit(1);
            }
        }
    }

#endif

    if (!xlu_cfg_get_list (config, "disk", &vbds, 0, 0)) {
        d_config->num_disks = 0;
        d_config->disks = NULL;
        while ((buf = xlu_cfg_get_listitem (vbds, d_config->num_disks)) != NULL) {
            libxl_device_disk *disk;
            char *buf2 = strdup(buf);

            d_config->disks = (libxl_device_disk *) realloc(d_config->disks, sizeof (libxl_device_disk) * (d_config->num_disks + 1));
            disk = d_config->disks + d_config->num_disks;
            parse_disk_config(&config, buf2, disk);

            free(buf2);
            d_config->num_disks++;
        }
    }

    if (!xlu_cfg_get_list(config, "vtpm", &vtpms, 0, 0)) {
        d_config->num_vtpms = 0;
        d_config->vtpms = NULL;
        while ((buf = xlu_cfg_get_listitem (vtpms, d_config->num_vtpms)) != NULL) {
            libxl_device_vtpm *vtpm;
            char * buf2 = strdup(buf);
            char *p, *p2;
            bool got_backend = false;

            d_config->vtpms = (libxl_device_vtpm *) realloc(d_config->vtpms,
                  sizeof(libxl_device_vtpm) * (d_config->num_vtpms+1));
            vtpm = d_config->vtpms + d_config->num_vtpms;
            libxl_device_vtpm_init(vtpm);
            vtpm->devid = d_config->num_vtpms;

            p = strtok(buf2, ",");
            if(p) {
               do {
                  while(*p == ' ')
                     ++p;
                  if ((p2 = strchr(p, '=')) == NULL)
                     break;
                  *p2 = '\0';
                  if (!strcmp(p, "backend")) {
                     vtpm->backend_domname = strdup(p2 + 1);
                     got_backend = true;
                  } else if(!strcmp(p, "uuid")) {
                     if( libxl_uuid_from_string(&vtpm->uuid, p2 + 1) ) {
                        fprintf(stderr,
                              "Failed to parse vtpm UUID: %s\n", p2 + 1);
                        exit(1);
                    }
                  } else {
                     fprintf(stderr, "Unknown string `%s' in vtpm spec\n", p);
                     exit(1);
                  }
               } while ((p = strtok(NULL, ",")) != NULL);
            }
            if(!got_backend) {
               fprintf(stderr, "vtpm spec missing required backend field!\n");
               exit(1);
            }
            free(buf2);
            d_config->num_vtpms++;
        }
    }
#if 0
    if (!xlu_cfg_get_list (config, "channel", &channels, 0, 0)) {
        d_config->num_channels = 0;
        d_config->channels = NULL;
        while ((buf = xlu_cfg_get_listitem (channels,
                d_config->num_channels)) != NULL) {
            libxl_device_channel *chn;
            libxl_string_list pairs;
            char *path = NULL;
            int len;

            chn = ARRAY_EXTEND_INIT(d_config->channels, d_config->num_channels,
                                   libxl_device_channel_init);

            split_string_into_string_list(buf, ",", &pairs);
            len = libxl_string_list_length(&pairs);
            for (i = 0; i < len; i++) {
                char *key, *key_untrimmed, *value, *value_untrimmed;
                int rc;
                rc = split_string_into_pair(pairs[i], "=",
                                            &key_untrimmed,
                                            &value_untrimmed);
                if (rc != 0) {
                    fprintf(stderr, "failed to parse channel configuration: %s",
                            pairs[i]);
                    exit(1);
                }
                trim(isspace, key_untrimmed, &key);
                trim(isspace, value_untrimmed, &value);

                if (!strcmp(key, "backend")) {
                    replace_string(&chn->backend_domname, value);
                } else if (!strcmp(key, "name")) {
                    replace_string(&chn->name, value);
                } else if (!strcmp(key, "path")) {
                    replace_string(&path, value);
                } else if (!strcmp(key, "connection")) {
                    if (!strcmp(value, "pty")) {
                        chn->connection = LIBXL_CHANNEL_CONNECTION_PTY;
                    } else if (!strcmp(value, "socket")) {
                        chn->connection = LIBXL_CHANNEL_CONNECTION_SOCKET;
                    } else {
                        fprintf(stderr, "unknown channel connection '%s'\n",
                                value);
                        exit(1);
                    }
                } else {
                    fprintf(stderr, "unknown channel parameter '%s',"
                                  " ignoring\n", key);
                }
                free(key);
                free(key_untrimmed);
                free(value);
                free(value_untrimmed);
            }
            switch (chn->connection) {
            case LIBXL_CHANNEL_CONNECTION_UNKNOWN:
                fprintf(stderr, "channel has unknown 'connection'\n");
                exit(1);
            case LIBXL_CHANNEL_CONNECTION_SOCKET:
                if (!path) {
                    fprintf(stderr, "channel connection 'socket' requires path=..\n");
                    exit(1);
                }
                chn->u.socket.path = xstrdup(path);
                break;
            case LIBXL_CHANNEL_CONNECTION_PTY:
                /* Nothing to do since PTY has no arguments */
                break;
            default:
                fprintf(stderr, "unknown channel connection: %d",
                        chn->connection);
                exit(1);
            }
            libxl_string_list_dispose(&pairs);
            free(path);
        }
    }
#endif

    if (!xlu_cfg_get_list (config, "vif", &nics, 0, 0)) {
        d_config->num_nics = 0;
        d_config->nics = NULL;
        while ((buf = xlu_cfg_get_listitem (nics, d_config->num_nics)) != NULL) {
            libxl_device_nic *nic;
            char *buf2 = strdup(buf);
            char *p;

            d_config->nics = (libxl_device_nic *) realloc(d_config->nics, sizeof (libxl_device_nic) * (d_config->num_nics+1));
            nic = d_config->nics + d_config->num_nics;
            libxl_device_nic_init(nic);
            nic->devid = d_config->num_nics;
            set_default_nic_values(nic);

            p = strtok(buf2, ",");
            if (!p)
                goto skip_nic;
            do {
                while (*p == ' ')
                    p++;
                parse_nic_config(nic, &config, p);
            } while ((p = strtok(NULL, ",")) != NULL);
skip_nic:
            free(buf2);
            d_config->num_nics++;
        }
    }

    if (!xlu_cfg_get_list(config, "vif2", NULL, 0, 0)) {
        fprintf(stderr, "WARNING: vif2: netchannel2 is deprecated and not supported by xl\n");
    }

    d_config->num_vfbs = 0;
    d_config->num_vkbs = 0;
    d_config->vfbs = NULL;
    d_config->vkbs = NULL;
#if 0
    if (!xlu_cfg_get_list (config, "vfb", &cvfbs, 0, 0)) {
        while ((buf = xlu_cfg_get_listitem (cvfbs, d_config->num_vfbs)) != NULL) {
            libxl_device_vfb *vfb;
            libxl_device_vkb *vkb;

            char *buf2 = strdup(buf);
            char *p, *p2;

            vfb = ARRAY_EXTEND_INIT(d_config->vfbs, d_config->num_vfbs,
                                    libxl_device_vfb_init);

            vkb = ARRAY_EXTEND_INIT(d_config->vkbs, d_config->num_vkbs,
                                    libxl_device_vkb_init);

            p = strtok(buf2, ",");
            if (!p)
                goto skip_vfb;
            do {
                while (*p == ' ')
                    p++;
                if ((p2 = strchr(p, '=')) == NULL)
                    break;
                *p2 = '\0';
                if (!strcmp(p, "vnc")) {
                    libxl_defbool_set(&vfb->vnc.enable, atoi(p2 + 1));
                } else if (!strcmp(p, "vnclisten")) {
                    free(vfb->vnc.listen);
                    vfb->vnc.listen = strdup(p2 + 1);
                } else if (!strcmp(p, "vncpasswd")) {
                    free(vfb->vnc.passwd);
                    vfb->vnc.passwd = strdup(p2 + 1);
                } else if (!strcmp(p, "vncdisplay")) {
                    vfb->vnc.display = atoi(p2 + 1);
                } else if (!strcmp(p, "vncunused")) {
                    libxl_defbool_set(&vfb->vnc.findunused, atoi(p2 + 1));
                } else if (!strcmp(p, "keymap")) {
                    free(vfb->keymap);
                    vfb->keymap = strdup(p2 + 1);
                } else if (!strcmp(p, "sdl")) {
                    libxl_defbool_set(&vfb->sdl.enable, atoi(p2 + 1));
                } else if (!strcmp(p, "opengl")) {
                    libxl_defbool_set(&vfb->sdl.opengl, atoi(p2 + 1));
                } else if (!strcmp(p, "display")) {
                    free(vfb->sdl.display);
                    vfb->sdl.display = strdup(p2 + 1);
                } else if (!strcmp(p, "xauthority")) {
                    free(vfb->sdl.xauthority);
                    vfb->sdl.xauthority = strdup(p2 + 1);
                }
            } while ((p = strtok(NULL, ",")) != NULL);

skip_vfb:
            free(buf2);
        }
    }
#endif

    if (!xlu_cfg_get_long (config, "pci_msitranslate", &l, 0))
        pci_msitranslate = l;

    if (!xlu_cfg_get_long (config, "pci_power_mgmt", &l, 0))
        pci_power_mgmt = l;

    if (!xlu_cfg_get_long (config, "pci_permissive", &l, 0))
        pci_permissive = l;

    if (!xlu_cfg_get_long (config, "pci_seize", &l, 0))
        pci_seize = l;

    /* To be reworked (automatically enabled) once the auto ballooning
     * after guest starts is done (with PCI devices passed in). */
    if (c_info->type == LIBXL_DOMAIN_TYPE_PV) {
        xlu_cfg_get_defbool(config, "e820_host", &b_info->u.pv.e820_host, 0);
    }

    if (!xlu_cfg_get_list (config, "pci", &pcis, 0, 0)) {
        d_config->num_pcidevs = 0;
        d_config->pcidevs = NULL;
        for(i = 0; (buf = xlu_cfg_get_listitem (pcis, i)) != NULL; i++) {
            libxl_device_pci *pcidev;

            d_config->pcidevs = (libxl_device_pci *) realloc(d_config->pcidevs, sizeof (libxl_device_pci) * (d_config->num_pcidevs + 1));
            pcidev = d_config->pcidevs + d_config->num_pcidevs;
            libxl_device_pci_init(pcidev);

            pcidev->msitranslate = pci_msitranslate;
            pcidev->power_mgmt = pci_power_mgmt;
            pcidev->permissive = pci_permissive;
            pcidev->seize = pci_seize;
            if (!xlu_pci_parse_bdf(config, pcidev, buf))
                d_config->num_pcidevs++;
        }
        if (d_config->num_pcidevs && c_info->type == LIBXL_DOMAIN_TYPE_PV)
            libxl_defbool_set(&b_info->u.pv.e820_host, true);
    }

    switch (xlu_cfg_get_list(config, "cpuid", &cpuids, 0, 1)) {
    case 0:
        {
            const char *errstr;

            for (i = 0; (buf = xlu_cfg_get_listitem(cpuids, i)) != NULL; i++) {
                e = libxl_cpuid_parse_config_xend(&b_info->cpuid, buf);
                switch (e) {
                case 0: continue;
                case 1:
                    errstr = "illegal leaf number";
                    break;
                case 2:
                    errstr = "illegal subleaf number";
                    break;
                case 3:
                    errstr = "missing colon";
                    break;
                case 4:
                    errstr = "invalid register name (must be e[abcd]x)";
                    break;
                case 5:
                    errstr = "policy string must be exactly 32 characters long";
                    break;
                default:
                    errstr = "unknown error";
                    break;
                }
                fprintf(stderr, "while parsing CPUID line: \"%s\":\n", buf);
                fprintf(stderr, "  error #%i: %s\n", e, errstr);
            }
        }
        break;
    case EINVAL:    /* config option is not a list, parse as a string */
        if (!xlu_cfg_get_string(config, "cpuid", &buf, 0)) {
            char *buf2, *p, *strtok_ptr = NULL;
            const char *errstr;

            buf2 = strdup(buf);
            p = strtok_r(buf2, ",", &strtok_ptr);
            if (p == NULL) {
                free(buf2);
                break;
            }
            if (strcmp(p, "host")) {
                fprintf(stderr, "while parsing CPUID string: \"%s\":\n", buf);
                fprintf(stderr, "  error: first word must be \"host\"\n");
                free(buf2);
                break;
            }
            for (p = strtok_r(NULL, ",", &strtok_ptr); p != NULL;
                 p = strtok_r(NULL, ",", &strtok_ptr)) {
                e = libxl_cpuid_parse_config(&b_info->cpuid, p);
                switch (e) {
                case 0: continue;
                case 1:
                    errstr = "missing \"=\" in key=value";
                    break;
                case 2:
                    errstr = "unknown CPUID flag name";
                    break;
                case 3:
                    errstr = "illegal CPUID value (must be: [0|1|x|k|s])";
                    break;
                default:
                    errstr = "unknown error";
                    break;
                }
                fprintf(stderr, "while parsing CPUID flag: \"%s\":\n", p);
                fprintf(stderr, "  error #%i: %s\n", e, errstr);
            }
            free(buf2);
        }
        break;
    default:
        break;
    }

    /* parse device model arguments, this works for pv, hvm and stubdom */
    if (!xlu_cfg_get_string (config, "device_model", &buf, 0)) {
        fprintf(stderr,
                "WARNING: ignoring device_model directive.\n"
                "WARNING: Use \"device_model_override\" instead if you"
                " really want a non-default device_model\n");
        if (strstr(buf, "stubdom-dm")) {
            if (c_info->type == LIBXL_DOMAIN_TYPE_HVM)
                fprintf(stderr, "WARNING: Or use"
                        " \"device_model_stubdomain_override\" if you "
                        " want to enable stubdomains\n");
            else
                fprintf(stderr, "WARNING: ignoring"
                        " \"device_model_stubdomain_override\" directive"
                        " for pv guest\n");
        }
    }


    xlu_cfg_replace_string (config, "device_model_override",
                            &b_info->device_model, 0);
    if (!xlu_cfg_get_string (config, "device_model_version", &buf, 0)) {
        if (!strcmp(buf, "qemu-xen-traditional")) {
            b_info->device_model_version
                = LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN_TRADITIONAL;
        } else if (!strcmp(buf, "qemu-xen")) {
            b_info->device_model_version
                = LIBXL_DEVICE_MODEL_VERSION_QEMU_XEN;
        } else {
            fprintf(stderr,
                    "Unknown device_model_version \"%s\" specified\n", buf);
            exit(1);
        }
    } else if (b_info->device_model)
        fprintf(stderr, "WARNING: device model override given without specific DM version\n");
    xlu_cfg_get_defbool (config, "device_model_stubdomain_override",
                         &b_info->device_model_stubdomain, 0);

    if (!xlu_cfg_get_string (config, "device_model_stubdomain_seclabel",
                             &buf, 0))
        xlu_cfg_replace_string(config, "device_model_stubdomain_seclabel",
                               &b_info->device_model_ssid_label, 0);

#define parse_extra_args(type)                                            \
    e = xlu_cfg_get_list_as_string_list(config, "device_model_args"#type, \
                                    &b_info->extra##type, 0);            \
    if (e && e != ESRCH) {                                                \
        fprintf(stderr,"xl: Unable to parse device_model_args"#type".\n");\
        exit(-ERROR_FAIL);                                                \
    }

    /* parse extra args for qemu, common to both pv, hvm */
    parse_extra_args();

    /* parse extra args dedicated to pv */
    parse_extra_args(_pv);

    /* parse extra args dedicated to hvm */
    parse_extra_args(_hvm);

#undef parse_extra_args

    /* If we've already got vfb=[] for PV guest then ignore top level
     * VNC config. */
    if (c_info->type == LIBXL_DOMAIN_TYPE_PV && !d_config->num_vfbs) {
        long vnc_enabled = 0;
        if (!xlu_cfg_get_long (config, "vnc", &l, 0))
            vnc_enabled = l;

        if (vnc_enabled) {
            libxl_device_vfb *vfb;
            libxl_device_vkb *vkb;

            vfb = ARRAY_EXTEND_INIT(d_config->vfbs, d_config->num_vfbs,
                                    libxl_device_vfb_init);

            vkb = ARRAY_EXTEND_INIT(d_config->vkbs, d_config->num_vkbs,
                                    libxl_device_vkb_init);

            parse_top_level_vnc_options(config, &vfb->vnc);
            parse_top_level_sdl_options(config, &vfb->sdl);
            xlu_cfg_replace_string (config, "keymap", &vfb->keymap, 0);
        }
    } else {
        parse_top_level_vnc_options(config, &b_info->u.hvm.vnc);
        parse_top_level_sdl_options(config, &b_info->u.hvm.sdl);
    }

    if (c_info->type == LIBXL_DOMAIN_TYPE_HVM) {
        if (!xlu_cfg_get_string (config, "vga", &buf, 0)) {
            if (!strcmp(buf, "stdvga")) {
                b_info->u.hvm.vga.kind = LIBXL_VGA_INTERFACE_TYPE_STD;
            } else if (!strcmp(buf, "cirrus")) {
                b_info->u.hvm.vga.kind = LIBXL_VGA_INTERFACE_TYPE_CIRRUS;
            } else if (!strcmp(buf, "none")) {
                b_info->u.hvm.vga.kind = LIBXL_VGA_INTERFACE_TYPE_NONE;
            } else {
                fprintf(stderr, "Unknown vga \"%s\" specified\n", buf);
                exit(1);
            }
        } else if (!xlu_cfg_get_long(config, "stdvga", &l, 0))
            b_info->u.hvm.vga.kind = l ? LIBXL_VGA_INTERFACE_TYPE_STD :
                                         LIBXL_VGA_INTERFACE_TYPE_CIRRUS;

        xlu_cfg_replace_string (config, "keymap", &b_info->u.hvm.keymap, 0);
        xlu_cfg_get_defbool (config, "spice", &b_info->u.hvm.spice.enable, 0);
        if (!xlu_cfg_get_long (config, "spiceport", &l, 0))
            b_info->u.hvm.spice.port = l;
        if (!xlu_cfg_get_long (config, "spicetls_port", &l, 0))
            b_info->u.hvm.spice.tls_port = l;
        xlu_cfg_replace_string (config, "spicehost",
                                &b_info->u.hvm.spice.host, 0);
        xlu_cfg_get_defbool(config, "spicedisable_ticketing",
                            &b_info->u.hvm.spice.disable_ticketing, 0);
        xlu_cfg_replace_string (config, "spicepasswd",
                                &b_info->u.hvm.spice.passwd, 0);
        xlu_cfg_get_defbool(config, "spiceagent_mouse",
                            &b_info->u.hvm.spice.agent_mouse, 0);
        xlu_cfg_get_defbool(config, "spicevdagent",
                            &b_info->u.hvm.spice.vdagent, 0);
        xlu_cfg_get_defbool(config, "spice_clipboard_sharing",
                            &b_info->u.hvm.spice.clipboard_sharing, 0);
        if (!xlu_cfg_get_long (config, "spiceusbredirection", &l, 0))
            b_info->u.hvm.spice.usbredirection = l;
        xlu_cfg_replace_string (config, "spice_image_compression",
                                &b_info->u.hvm.spice.image_compression, 0);
        xlu_cfg_replace_string (config, "spice_streaming_video",
                                &b_info->u.hvm.spice.streaming_video, 0);
        xlu_cfg_get_defbool(config, "nographic", &b_info->u.hvm.nographic, 0);
        xlu_cfg_get_defbool(config, "gfx_passthru",
                            &b_info->u.hvm.gfx_passthru, 0);
        switch (xlu_cfg_get_list_as_string_list(config, "serial",
                                                &b_info->u.hvm.serial_list,
                                                1))
        {

        case 0: break; /* Success */
        case ESRCH: break; /* Option not present */
        case EINVAL:
            /* If it's not a valid list, try reading it as an atom,
             * falling through to an error if it fails */
            if (!xlu_cfg_replace_string(config, "serial",
                                        &b_info->u.hvm.serial, 0))
                break;
            /* FALLTHRU */
        default:
            fprintf(stderr,"xl: Unable to parse serial.\n");
            exit(-ERROR_FAIL);
        }
        xlu_cfg_replace_string (config, "boot", &b_info->u.hvm.boot, 0);
        xlu_cfg_get_defbool(config, "usb", &b_info->u.hvm.usb, 0);
        if (!xlu_cfg_get_long (config, "usbversion", &l, 0))
            b_info->u.hvm.usbversion = l;
        switch (xlu_cfg_get_list_as_string_list(config, "usbdevice",
                                                &b_info->u.hvm.usbdevice_list,
                                                1))
        {

        case 0: break; /* Success */
        case ESRCH: break; /* Option not present */
        case EINVAL:
            /* If it's not a valid list, try reading it as an atom,
             * falling through to an error if it fails */
            if (!xlu_cfg_replace_string(config, "usbdevice",
                                        &b_info->u.hvm.usbdevice, 0))
                break;
            /* FALLTHRU */
        default:
            fprintf(stderr,"xl: Unable to parse usbdevice.\n");
            exit(-ERROR_FAIL);
        }
        xlu_cfg_replace_string (config, "soundhw", &b_info->u.hvm.soundhw, 0);
        xlu_cfg_get_defbool(config, "xen_platform_pci",
                            &b_info->u.hvm.xen_platform_pci, 0);

        if(b_info->u.hvm.vnc.listen
           && b_info->u.hvm.vnc.display
           && strchr(b_info->u.hvm.vnc.listen, ':') != NULL) {
            fprintf(stderr,
                    "ERROR: Display specified both in vnclisten"
                    " and vncdisplay!\n");
            exit (1);

        }

        if (!xlu_cfg_get_string (config, "vendor_device", &buf, 0)) {
            libxl_vendor_device d;

            e = libxl_vendor_device_from_string(buf, &d);
            if (e) {
                fprintf(stderr,
                        "xl: unknown vendor_device '%s'\n",
                        buf);
                exit(-ERROR_FAIL);
            }

            b_info->u.hvm.vendor_device = d;
        }
    }

    xlu_cfg_destroy(config);
}
#if 0
static int acquire_lock(void)
{
    int rc;
    struct flock fl;

    /* lock already acquired */
    if (fd_lock >= 0)
        return ERROR_INVAL;

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fd_lock = open(lockfile, O_WRONLY|O_CREAT, S_IWUSR);
    if (fd_lock < 0) {
        fprintf(stderr, "cannot open the lockfile %s errno=%d\n", lockfile, errno);
        return ERROR_FAIL;
    }
    if (fcntl(fd_lock, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd_lock);
        fprintf(stderr, "cannot set cloexec to lockfile %s errno=%d\n", lockfile, errno);
        return ERROR_FAIL;
    }
get_lock:
    rc = fcntl(fd_lock, F_SETLKW, &fl);
    if (rc < 0 && errno == EINTR)
        goto get_lock;
    if (rc < 0) {
        fprintf(stderr, "cannot acquire lock %s errno=%d\n", lockfile, errno);
        rc = ERROR_FAIL;
    } else
        rc = 0;
    return rc;
}
#endif

int freemem(uint32_t domid, libxl_domain_build_info *b_info)
{
    int rc, retries = 3;
    uint32_t need_memkb, free_memkb;

    if (!autoballoon)
        return 0;

    rc = libxl_domain_need_memory(ctx, b_info, &need_memkb);
    if (rc < 0)
        return rc;

    do {
        rc = libxl_get_free_memory(ctx, &free_memkb);
        if (rc < 0)
            return rc;

        if (free_memkb >= need_memkb)
            return 0;

        rc = libxl_set_memory_target(ctx, 0, free_memkb - need_memkb, 1, 0);
        if (rc < 0)
            return rc;

        /* wait until dom0 reaches its target, as long as we are making
         * progress */
        rc = libxl_wait_for_memory_target(ctx, 0, 10);
        if (rc < 0)
            return rc;

        retries--;
    } while (retries > 0);

    return ERROR_NOMEM;
}

/******************  END OF FILE *****************************/
