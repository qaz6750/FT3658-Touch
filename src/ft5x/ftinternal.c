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
#include <device.h>
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
      NTSTATUS status;
      UINT8 point[FTS_TOUCH_BUFFER_LENGTH] = { 0 };
      UINT8 pointCount;
      UINT8 activeCount = 0;
      UINT32 seenIds = 0;

      UNREFERENCED_PARAMETER(ControllerContext);
      RtlZeroMemory(Data, sizeof(*Data));
    point[0] = 0x1;

      status = FTS_Read(SpbContext, point, point + 1, sizeof(point) - 1);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, TRACE_INTERRUPT, "failed to read finger status data %!STATUS!", status);
        goto exit;
    }

      pointCount = point[2] & 0x0F;
      if (pointCount > MAX_TOUCHES)
      {
            return STATUS_DATA_ERROR;
      }

      for (UINT32 slot = 0; slot < MAX_TOUCHES; slot++)
      {
            UINT32 base = FTS_TOUCH_RECORD_LENGTH * slot;
            UINT8 inputId = point[5 + base] >> 4;
            UINT8 event = point[3 + base] >> 6;

            if (inputId >= MAX_TOUCHES)
            {
                  break;
            }
            if ((seenIds & (1UL << inputId)) != 0 || event == FOCAL_TECH_EVENT_NONE)
            {
                  return STATUS_DATA_ERROR;
            }
            seenIds |= 1UL << inputId;

            if (event == FOCAL_TECH_EVENT_PRESS_DOWN || event == FOCAL_TECH_EVENT_CONTACT)
            {
                  int positionX = ((point[3 + base] & 0x0F) << 11) |
                        (point[4 + base] << 3) | ((point[7 + base] & 0xE0) >> 5);
                  int positionY = ((point[5 + base] & 0x0F) << 11) |
                        (point[6 + base] << 3) | ((point[7 + base] & 0x1C) >> 2);

                  if (pointCount == 0 || positionX >= FTS_TOUCH_X_RANGE || positionY >= FTS_TOUCH_Y_RANGE)
                  {
                        return STATUS_DATA_ERROR;
                  }

                  Data->States[inputId] = OBJECT_STATE_FINGER_PRESENT_WITH_ACCURATE_POS;
                  Data->Positions[inputId].X = positionX;
                  Data->Positions[inputId].Y = positionY;
                  activeCount++;
            }
      }

      if (activeCount != pointCount)
      {
            return STATUS_DATA_ERROR;
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

      WdfWaitLockAcquire(ControllerContext->ControllerLock, NULL);
      if (ControllerContext->DevicePowerState == PowerDeviceD0 && !ControllerContext->DisplayOff)
      {
            status = TchServiceObjectInterrupts(ControllerContext, SpbContext, ReportContext);
      }
      WdfWaitLockRelease(ControllerContext->ControllerLock);
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
      UINT8 command[2] = { FTS_REG_POWER_MODE, FTS_POWER_MODE_SLEEP };
      NTSTATUS status;

      if (SleepState == FT5X_F01_DEVICE_CONTROL_SLEEP_MODE_SLEEPING)
      {
            return FTS_Write(SpbContext, command, sizeof(command));
      }
      if (SleepState != FT5X_F01_DEVICE_CONTROL_SLEEP_MODE_OPERATING)
      {
            return STATUS_INVALID_PARAMETER;
      }

      status = TchResetController(ControllerContext->FxDevice);
      if (!NT_SUCCESS(status))
      {
            return status;
      }
      return TchStartDevice(ControllerContext, SpbContext);
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