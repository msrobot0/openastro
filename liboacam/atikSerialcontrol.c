/*****************************************************************************
 *
 * atikSerialcontrol.c -- control functions for Atik serial cameras
 *
 * Copyright 2014,2015,2016,2017 James Fidell (james@openastroproject.org)
 *
 * License:
 *
 * This file is part of the Open Astro Project.
 *
 * The Open Astro Project is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The Open Astro Project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Open Astro Project.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 *****************************************************************************/

#include <oa_common.h>

#include <pthread.h>
#include <libusb-1.0/libusb.h>

#include <openastro/camera.h>
#include <openastro/util.h>

#include "oacamprivate.h"
#include "atikSerial.h"
#include "atikSerialoacam.h"
#include "atikSerialstate.h"


int
oaAtikSerialCameraSetControl ( oaCamera* camera, int control,
    oaControlValue* val, int dontWait )
{
  OA_COMMAND		command;
  AtikSerial_STATE*	cameraInfo = camera->_private;
  int		retval = OA_ERR_NONE;

  // Could do more validation here, but it's a bit messy to do here
  // and in the controller too.

  OA_CLEAR ( command );
  command.commandType = OA_CMD_CONTROL_SET;
  command.controlId = control;
  command.commandData = val;

  cameraInfo = camera->_private;
  oaDLListAddToTail ( cameraInfo->commandQueue, &command );
  pthread_cond_broadcast ( &cameraInfo->commandQueued );
  if ( !dontWait ) {
    pthread_mutex_lock ( &cameraInfo->commandQueueMutex );
    while ( !command.completed ) {
      pthread_cond_wait ( &cameraInfo->commandComplete,
          &cameraInfo->commandQueueMutex );
    }
    pthread_mutex_unlock ( &cameraInfo->commandQueueMutex );
    retval = command.resultCode;
  }

  return retval;
}


int
oaAtikSerialCameraReadControl ( oaCamera* camera, int control,
    oaControlValue* val )
{
  OA_COMMAND		command;
  AtikSerial_STATE*	cameraInfo = camera->_private;
  int			retval;

  // Could do more validation here, but it's a bit messy to do here
  // and in the controller too.

  OA_CLEAR ( command );
  command.commandType = OA_CMD_CONTROL_GET;
  command.controlId = control;
  command.resultData = val;

  cameraInfo = camera->_private;
  oaDLListAddToTail ( cameraInfo->commandQueue, &command );
  pthread_cond_broadcast ( &cameraInfo->commandQueued );
  pthread_mutex_lock ( &cameraInfo->commandQueueMutex );
  while ( !command.completed ) {
    pthread_cond_wait ( &cameraInfo->commandComplete,
        &cameraInfo->commandQueueMutex );
  }
  pthread_mutex_unlock ( &cameraInfo->commandQueueMutex );
  retval = command.resultCode;

  return retval;
}


int
oaAtikSerialCameraTestControl ( oaCamera* camera, int control,
    oaControlValue* val )
{
  uint32_t	val_u32;
  int64_t	val_s64;
  COMMON_INFO*	commonInfo = camera->_common;

  if ( !camera->OA_CAM_CTRL_TYPE( control )) {
    return -OA_ERR_INVALID_CONTROL;
  }

  if ( camera->OA_CAM_CTRL_TYPE( control ) != val->valueType ) {
    return -OA_ERR_INVALID_CONTROL_TYPE;
  }

  switch ( control ) {

    case OA_CAM_CTRL_BRIGHTNESS:
    case OA_CAM_CTRL_CONTRAST:
    case OA_CAM_CTRL_SATURATION:
    case OA_CAM_CTRL_HUE:
    case OA_CAM_CTRL_SHARPNESS:
    case OA_CAM_CTRL_GAMMA:
    case OA_CAM_CTRL_WHITE_BALANCE_TEMP:
    case OA_CAM_CTRL_GAIN:
      // This should be unsigned 32-bit
      val_s64 = val->int64;
      if ( val_s64 < 0 ) {
        return -OA_ERR_OUT_OF_RANGE;
      }
      val_u32 = val_s64 & 0xffffffff;
      if ( val_u32 >= commonInfo->OA_CAM_CTRL_MIN( control ) &&
          val_u32 <= commonInfo->OA_CAM_CTRL_MAX( control ) &&
          ( 0 == ( val_u32 - commonInfo->OA_CAM_CTRL_MIN( control )) %
          commonInfo->OA_CAM_CTRL_STEP( control ))) {
        return OA_ERR_NONE;
      }
      break;

    case OA_CAM_CTRL_EXPOSURE_ABSOLUTE:
      val_s64 = val->int64;
      if ( val_s64 <= 0 ) {
        return -OA_ERR_OUT_OF_RANGE;
      }
      return OA_ERR_NONE;

    case OA_CAM_CTRL_MODE_AUTO( OA_CAM_CTRL_WHITE_BALANCE ):
    case OA_CAM_CTRL_AUTO_WHITE_BALANCE_TEMP:
    case OA_CAM_CTRL_MODE_AUTO( OA_CAM_CTRL_HUE ):
      // These just need to be boolean and we've already checked that
      return OA_ERR_NONE;
      break;

    case OA_CAM_CTRL_MODE_AUTO( OA_CAM_CTRL_EXPOSURE_ABSOLUTE ):
      if ( val->int32 != OA_EXPOSURE_AUTO && val->int32 !=
          OA_EXPOSURE_MANUAL ) {
        return -OA_ERR_OUT_OF_RANGE;
      }
      return OA_ERR_NONE;
      break;     

    case OA_CAM_CTRL_BIT_DEPTH:
    case OA_CAM_CTRL_BINNING:
      return -OA_ERR_INVALID_CONTROL;
      break;

    default:
      // If we reach here it's because we don't recognise the control
      fprintf ( stderr, "Unrecognised control %d in %s\n", control,
          __FUNCTION__ );
      return -OA_ERR_INVALID_CONTROL;
      break;
  }

  // And if we reach here it's because the value wasn't valid
  return -OA_ERR_OUT_OF_RANGE;
}


int
oaAtikSerialCameraSetResolution ( oaCamera* camera, int x, int y )
{
  FRAMESIZE		s;
  OA_COMMAND		command;
  AtikSerial_STATE*	cameraInfo = camera->_private;
  int		retval;

  OA_CLEAR ( command );
  command.commandType = OA_CMD_RESOLUTION_SET;
  s.x = x;
  s.y = y;
  command.commandData = &s;
  cameraInfo = camera->_private;
  oaDLListAddToTail ( cameraInfo->commandQueue, &command );
  pthread_cond_broadcast ( &cameraInfo->commandQueued );
  pthread_mutex_lock ( &cameraInfo->commandQueueMutex );
  while ( !command.completed ) {
    pthread_cond_wait ( &cameraInfo->commandComplete,
        &cameraInfo->commandQueueMutex );
  }
  pthread_mutex_unlock ( &cameraInfo->commandQueueMutex );
  retval = command.resultCode;

  return retval;
}


int
oaAtikSerialCameraStartStreaming ( oaCamera* camera,
    void* (*callback)(void*, void*, int), void* callbackArg )
{
  OA_COMMAND		command;
  CALLBACK		callbackData;
  int			retval;
  AtikSerial_STATE*	cameraInfo = camera->_private;

  oacamDebugMsg ( DEBUG_CAM_CTRL, "AtikSerial: control: %s ( %p )\n",
      __FUNCTION__, callback );

  OA_CLEAR ( command );
  callbackData.callback = callback;
  callbackData.callbackArg = callbackArg;
  command.commandType = OA_CMD_START;
  command.commandData = ( void* ) &callbackData;

  oaDLListAddToTail ( cameraInfo->commandQueue, &command );
  pthread_cond_broadcast ( &cameraInfo->commandQueued );
  pthread_mutex_lock ( &cameraInfo->commandQueueMutex );
  while ( !command.completed ) {
    pthread_cond_wait ( &cameraInfo->commandComplete,
        &cameraInfo->commandQueueMutex );
  }
  pthread_mutex_unlock ( &cameraInfo->commandQueueMutex );
  retval = command.resultCode;

  return retval;
}


int
oaAtikSerialCameraIsStreaming ( oaCamera* camera )
{
  AtikSerial_STATE*  cameraInfo = camera->_private;

  return ( cameraInfo->isStreaming );
}


int
oaAtikSerialCameraStopStreaming ( oaCamera* camera )
{
  OA_COMMAND		command;
  int			retval;
  AtikSerial_STATE*	cameraInfo = camera->_private;

  oacamDebugMsg ( DEBUG_CAM_CTRL, "AtikSerial: control: %s()\n", __FUNCTION__ );

  OA_CLEAR ( command );
  command.commandType = OA_CMD_STOP;

  oaDLListAddToTail ( cameraInfo->commandQueue, &command );
  pthread_cond_broadcast ( &cameraInfo->commandQueued );
  pthread_mutex_lock ( &cameraInfo->commandQueueMutex );
  while ( !command.completed ) {
    pthread_cond_wait ( &cameraInfo->commandComplete,
        &cameraInfo->commandQueueMutex );
  }
  pthread_mutex_unlock ( &cameraInfo->commandQueueMutex );
  retval = command.resultCode;

  return retval;
}
