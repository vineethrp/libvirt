/*
 * Copyright Intel Corp. 2020
 *
 * ch_driver.h: header file for Cloud-Hypervisor driver functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <unistd.h>
#include <fcntl.h>

#include "ch_domain.h"
#include "ch_monitor.h"
#include "ch_process.h"
#include "viralloc.h"
#include "virerror.h"
#include "virlog.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_process");

#define START_SOCKET_POSTFIX ": starting up socket\n"
#define START_VM_POSTFIX ": starting up vm\n"



static virCHMonitorPtr virCHProcessConnectMonitor(virCHDriverPtr driver,
                                                  virDomainObjPtr vm)
{
    virCHMonitorPtr monitor = NULL;
    virCHDriverConfigPtr cfg = virCHDriverGetConfig(driver);

    monitor = virCHMonitorNew(vm, cfg->stateDir);

    virObjectUnref(cfg);
    return monitor;
}

/**
 * virCHProcessStart:
 * @driver: pointer to driver structure
 * @vm: pointer to virtual machine structure
 * @reason: reason for switching vm to running state
 *
 * Starts Cloud-Hypervisor listen on a local socket
 *
 * Returns 0 on success or -1 in case of error
 */
int virCHProcessStart(virCHDriverPtr driver,
                      virDomainObjPtr vm,
                      virDomainRunningReason reason)
{
    virCHDomainObjPrivatePtr priv = vm->privateData;
    g_autofree int *nicindexes = NULL;
    size_t nnicindexes = 0;
    int ret = -1;

    if (!priv->monitor) {
        /* And we can get the first monitor connection now too */
        if (!(priv->monitor = virCHProcessConnectMonitor(driver, vm))) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                        _("failed to create connection to CH socket"));
            goto cleanup;
        }

        if (virCHMonitorCreateVM(priv->monitor,
                                 &nnicindexes, &nicindexes) < 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                        _("failed to create guest VM"));
            goto cleanup;
        }
    }

    if (virCHMonitorBootVM(priv->monitor) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("failed to boot guest VM"));
        goto cleanup;
    }

    vm->pid = priv->monitor->pid;
    vm->def->id = vm->pid;
    virDomainObjSetState(vm, VIR_DOMAIN_RUNNING, reason);

    virCHDomainRefreshVcpuInfo(vm);

    return 0;

 cleanup:
    if (ret)
        virCHProcessStop(driver, vm, VIR_DOMAIN_SHUTOFF_FAILED);

    return ret;
}

int virCHProcessStop(virCHDriverPtr driver G_GNUC_UNUSED,
                     virDomainObjPtr vm,
                     virDomainShutoffReason reason)
{
    virCHDomainObjPrivatePtr priv = vm->privateData;

    VIR_DEBUG("Stopping VM name=%s pid=%d reason=%d",
              vm->def->name, (int)vm->pid, (int)reason);

    if (priv->monitor) {
        virCHMonitorClose(priv->monitor);
        priv->monitor = NULL;
    }

    vm->pid = -1;
    vm->def->id = -1;

    virDomainObjSetState(vm, VIR_DOMAIN_SHUTOFF, reason);

    return 0;
}
