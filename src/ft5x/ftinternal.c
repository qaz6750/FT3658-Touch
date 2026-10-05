/*++
      Copyright (c) Microsoft Corporation. All Rights Reserved.
      Sample code. Dealpoint ID #843729.

      Module Name:

            rmiinternal.c

      Abstract:

            Contains Synaptics initialization code

      Environment:

            Kernel mode

      Revision History:

--*/

#include <Cross Platform Shim\compat.h>
#include <report.h>
#include <ft5x\ftinternal.h>
#include <ft5x\ftfwupdate.h>
#include <ftinternal.tmh>
#include <_spb.h>

NTSTATUS
Ft5xBuildFunctionsTable(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangePage(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN int DesiredPage
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(DesiredPage);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xConfigureFunctions(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
      NTSTATUS status = STATUS_DEVICE_NOT_READY;
      UINT8 chipId[2] = { 0 };
      UINT8 command;
      UINT8 settings[2];
      LARGE_INTEGER interval;

      ControllerContext->MaxFingers = MAX_TOUCHES;
      interval.QuadPart = -100 * 10000;

      for (UINT32 attempt = 0; attempt < 10; attempt++)
      {
            command = FTS_REG_CHIP_ID;
            status = FTS_Read(SpbContext, &command, &chipId[0], 1);
            if (NT_SUCCESS(status))
            {
                  command = FTS_REG_CHIP_ID2;
                  status = FTS_Read(SpbContext, &command, &chipId[1], 1);
            }

            if (NT_SUCCESS(status))
            {
                  if (chipId[0] == FTS_CHIP_ID_HIGH && chipId[1] == FTS_CHIP_ID_LOW)
                  {
                        break;
                  }
                  status = STATUS_DEVICE_NOT_READY;
            }
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
      }

      if (!NT_SUCCESS(status))
      {
            Trace(TRACE_LEVEL_ERROR, TRACE_INTERRUPT,
                  "FT3658 application not ready, ID: 0x%02x%02x, status: 0x%08lX",
                  chipId[0], chipId[1], status);
            return status;
      }

      settings[0] = FTS_REG_GESTURE_ENABLE;
      settings[1] = 0;
      status = FTS_Write(SpbContext, settings, sizeof(settings));
      if (!NT_SUCCESS(status))
      {
            return status;
      }

      settings[0] = FTS_REG_DIFFDATA_ENABLE;
      return FTS_Write(SpbContext, settings, sizeof(settings));
}

NTSTATUS
Ft5xGetObjectStatusFromControllerF12(
      IN VOID* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN DETECTED_OBJECTS* Data
)
/*++

Routine Description:

      This routine reads raw touch messages from hardware. If there is
      no touch data available (if a non-touch interrupt fired), the
      function will not return success and no touch data was transferred.

Arguments:

      ControllerContext - Touch controller context
      SpbContext - A pointer to the current i2c context
      Data - A pointer to any returned F11 touch data

Return Value:

      NTSTATUS, where only success indicates data was returned

--*/
{
    NTSTATUS status = STATUS_SUCCESS;
    FT5X_CONTROLLER_CONTEXT* controller;
    controller = (FT5X_CONTROLLER_CONTEXT*)ControllerContext;

    UINT32 base = 0;

    UINT8 input_id = 0;
    UINT8 point[91] = { 0x0 };
    point[0] = 0x1;

    status = FTS_Read(SpbContext, point, point + 1, 90);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, TRACE_INTERRUPT, "failed to read finger status data %!STATUS!", status);
        goto exit;
    }

    for (UINT8 i = 0; i < 10; i++) {
        base = 6 * i;
        input_id = point[5 + base] >> 4;
        if (input_id >= 10)
            continue;

        if ((point[3 + base] >> 6) == 0x0 || (point[3 + base] >> 6) == 0x2) {
            Data->States[input_id] = OBJECT_STATE_FINGER_PRESENT_WITH_ACCURATE_POS;
            Data->Positions[input_id].X = ((point[3 + base] & 0x0F) << 8) + (point[4 + base] & 0xFF);
            Data->Positions[input_id].Y = ((point[5 + base] & 0x0F) << 8) + (point[6 + base] & 0xFF);
       }
    }

exit:
    return status;
}

NTSTATUS
TchServiceObjectInterrupts(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN PREPORT_CONTEXT ReportContext
)
{
      NTSTATUS status = STATUS_SUCCESS;
      DETECTED_OBJECTS data;

      RtlZeroMemory(&data, sizeof(data));

      //
      // See if new touch data is available
      //
      status = Ft5xGetObjectStatusFromControllerF12(
            ControllerContext,
            SpbContext,
            &data
      );

      if (!NT_SUCCESS(status))
      {
            Trace(
                  TRACE_LEVEL_VERBOSE,
                  TRACE_SAMPLES,
                  "No object data to report - 0x%08lX",
                  status);

            goto exit;
      }

      status = ReportObjects(
            ReportContext,
            data);

      if (!NT_SUCCESS(status))
      {
            Trace(
                  TRACE_LEVEL_VERBOSE,
                  TRACE_SAMPLES,
                  "Error while reporting objects - 0x%08lX",
                  status);

            goto exit;
      }

exit:
      return status;
}


NTSTATUS
Ft5xServiceInterrupts(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN PREPORT_CONTEXT ReportContext
)
{
      NTSTATUS status = STATUS_SUCCESS;

      TchServiceObjectInterrupts(ControllerContext, SpbContext, ReportContext);

      return status;
}

NTSTATUS
Ft5xSetReportingFlagsF12(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR NewMode,
    OUT UCHAR* OldMode
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(NewMode);
      UNREFERENCED_PARAMETER(OldMode);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangeChargerConnectedState(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR ChargerConnectedState
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(ChargerConnectedState);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangeSleepState(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR SleepState
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(SleepState);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xGetFirmwareVersion(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
      UINT8 command = FTS_REG_FW_VERSION;
      NTSTATUS status = FTS_Read(SpbContext, &command, &ControllerContext->FirmwareVersion, 1);

      if (NT_SUCCESS(status))
      {
            Trace(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPT,
                  "FT3658 application ID: 0x5652, firmware version: 0x%02x",
                  ControllerContext->FirmwareVersion);
      }
      return status;
}

NTSTATUS
Ft5xCheckInterrupts(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN ULONG* InterruptStatus
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(InterruptStatus);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xConfigureInterruptEnable(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);

      return STATUS_SUCCESS;
}