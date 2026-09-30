#ifndef __CAMERA_PATCHES_H__
#define __CAMERA_PATCHES_H__

#include "patches.h"
#include "camera.h"
#include "olib.h"
#include "quake.h"
#include "letterbox.h"
#include "one_point_cutscene.h"
#include "sys_math3d.h"
#include "controller.h"
#include "db_camera.h"
#include "cutscene_spline.h"
#include "libc64/qrand.h"
#include "src/overlays/actors/ovl_En_Horse/z_en_horse.h"

// Called by patched camera modes before calculating the eye position, overriding its pitch and yaw if the analog camera is active.
void analog_cam_apply(Camera* camera, VecGeo* eye_geo);

bool get_analog_cam_active();
void set_analog_cam_active(bool isActive);
void skip_analog_cam_once();

#endif
