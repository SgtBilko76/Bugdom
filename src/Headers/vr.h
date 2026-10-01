// VR.H
// OpenXR support for standalone VR headsets (Meta Quest).

#pragma once

#if __ANDROID__
	#define VR_ENABLED 1
#else
	#define VR_ENABLED 0
#endif

#if VR_ENABLED

#include <QD3D.h>
#include <stdbool.h>

bool VR_Init(void);								// call once the GL context is current
void VR_Shutdown(void);
bool VR_IsActive(void);							// true if we have an OpenXR session

bool VR_BeginFrame(void);						// waits for the next frame; true if the frame must be rendered
void VR_EndFrame(float fadeFactor);				// always call after VR_BeginFrame

int  VR_GetEyeCount(void);
void VR_BindEyeTarget(int eye);
void VR_BindUITarget(void);
void VR_ClearTargets(void);

// Computes per-eye camera matrices from the game's camera (from/to), plus
// a wide "center eye" frustum used for culling and depth-sorting.
void VR_ComputeCameraMatrices(
		const TQ3Point3D* from,
		const TQ3Point3D* to,
		float hither,
		float yon,
		bool useRigYaw,
		TQ3Matrix4x4* centerWorldToView,
		TQ3Matrix4x4* centerViewToFrustum);

const TQ3Matrix4x4* VR_GetEyeWorldToView(int eye);
const TQ3Matrix4x4* VR_GetEyeViewToFrustum(int eye);

// The "rig" yaw is the direction the chase camera faces during gameplay.
// It only changes via snap turns, which keeps the view comfortable.
float VR_GetRigYaw(void);
void  VR_SetRigYaw(float yaw);
bool  VR_ConsumeSnapTurn(void);					// true once after each snap turn

// Laser pointer (right controller) for point-and-click menus
void VR_SetPointerEnabled(bool enabled);
bool VR_GetPointerRay(TQ3Point3D* outOrigin, TQ3Vector3D* outDir);
void VR_SetPointerHitDistance(float distance);
void VR_DrawPointer(void);

// Recenter: current head position becomes the origin, current head yaw becomes "forward".
// Also triggered by holding the right thumbstick click for 1 second.
void VR_RequestRecenter(void);

#define VR_UI_WIDTH		1280
#define VR_UI_HEIGHT	960

#endif
