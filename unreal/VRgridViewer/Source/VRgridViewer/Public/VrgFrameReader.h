// The .vrgf reader, and the one place VRgrid coordinates become Unreal ones.
//
//     VRgrid world   x FORWARD, y LEFT,  z UP, RIGHT-handed, METRES
//     Unreal world   x FORWARD, y RIGHT, z UP, LEFT-handed,  CENTIMETRES
//
// So (x, y, z)_m -> (100x, -100y, 100z)_cm. The y negation is the whole of the
// handedness change, and dropping it produces a mirror image that looks
// entirely correct until someone puts it beside the Rerun window and finds the
// kerb on the wrong side of the car. `exporter/vrgrid_unreal/convert.py` is the
// reference; `FVrgScene::ValidateConversion` re-checks this code against
// vectors the exporter wrote into scene.json, at load, so a sign error is a
// startup error rather than something discovered on stage.
//
// Format: see exporter/vrgrid_unreal/format.py. Little-endian, 64-byte header,
// then self-describing chunks. Unknown chunk ids MUST be skipped by their
// byte length -- that is what lets the exporter add a layer without a rebuild.

#pragma once

#include "CoreMinimal.h"

namespace VrgFormat
{
	static constexpr uint32 Magic = 0x46475256;   // 'VRGF' little-endian
	static constexpr uint32 Version = 2;
	static constexpr int32 HeaderBytes = 64;
	static constexpr int32 ChunkHeaderBytes = 16;

	static constexpr float MetresToCm = 100.0f;
}

#pragma pack(push, 1)

/** world/points and world/ghosts. 16 B. */
struct FVrgPoint
{
	float X, Y, Z;
	uint8 R, G, B, A;
};

/** Any cell layer: occupied / free / unknown / confidence. 24 B. */
struct FVrgCell
{
	float X, Y, Z;
	float CellM;          // the cell's own edge length -- this is the foveation
	float SigmaCm;        // one sigma of the height estimate; 0 = not reported
	uint8 R, G, B, A;
};

/** Curbs and potholes, drawn at their measured rise / depth. 28 B. */
struct FVrgBox
{
	float X, Y, Z;
	float HX, HY, HZ;
	uint8 R, G, B, A;
};

/** The trajectory polyline. 12 B. */
struct FVrgVec3
{
	float X, Y, Z;
};

#pragma pack(pop)

static_assert(sizeof(FVrgPoint) == 16, "FVrgPoint must match POINT_DTYPE");
static_assert(sizeof(FVrgCell) == 24, "FVrgCell must match CELL_DTYPE");
static_assert(sizeof(FVrgBox) == 28, "FVrgBox must match BOX_DTYPE");
static_assert(sizeof(FVrgVec3) == 12, "FVrgVec3 must match VEC3_DTYPE");

/**
 * One decoded frame.
 *
 * A layer that is PRESENT but empty means "this layer is empty now -- clear
 * it". A layer that is ABSENT means "no new data this frame -- keep what is on
 * screen". The map layers are only recomputed every MapInterval frames, so the
 * distinction is load-bearing: conflate them and a ghost trail outlives the
 * cleanup that removed it. `bHas*` carries which of the two it was.
 */
struct FVrgFrame
{
	int32 FrameIndex = -1;
	FVector VehicleLocation = FVector::ZeroVector;   // already Unreal cm
	float VehicleYawDeg = 0.0f;                      // already Unreal degrees
	float TimeSeconds = 0.0f;

	TArray<FVrgPoint> Points;
	TArray<FVrgPoint> Ghosts;
	TArray<FVrgCell> Occupied;
	TArray<FVrgCell> Free;
	TArray<FVrgCell> Unknown;
	TArray<FVrgCell> Confidence;
	TArray<FVrgBox> Curbs;
	TArray<FVrgBox> Potholes;
	TArray<FVrgVec3> Trajectory;

	bool bHasPoints = false;
	bool bHasGhosts = false;
	bool bHasOccupied = false;
	bool bHasFree = false;
	bool bHasUnknown = false;
	bool bHasConfidence = false;
	bool bHasCurbs = false;
	bool bHasPotholes = false;
	bool bHasTrajectory = false;
};

class VRGRIDVIEWER_API FVrgFrameReader
{
public:
	/** Load and decode one .vrgf. Returns false and fills OutError on failure. */
	static bool LoadFrame(const FString& Path, FVrgFrame& OutFrame, FString& OutError);

	// --- the conversion, used everywhere and nowhere else ---

	/** VRgrid world metres -> Unreal world centimetres. */
	static FORCEINLINE FVector ToUnreal(float X, float Y, float Z)
	{
		return FVector(
			static_cast<double>(X) * VrgFormat::MetresToCm,
			static_cast<double>(Y) * -VrgFormat::MetresToCm,   // +y is LEFT in VRgrid
			static_cast<double>(Z) * VrgFormat::MetresToCm);
	}

	/** A length (cell edge, ring radius) in metres -> centimetres. */
	static FORCEINLINE double LengthToUnreal(float Metres)
	{
		return static_cast<double>(Metres) * VrgFormat::MetresToCm;
	}

	/** Right-handed yaw about +z (radians) -> Unreal yaw (degrees). */
	static FORCEINLINE float YawToUnrealDeg(float YawRadians)
	{
		return -FMath::RadiansToDegrees(YawRadians);
	}

	/** uint8 RGBA -> the float4 an instanced material reads as custom data. */
	static FORCEINLINE void ColorToCustomData(uint8 R, uint8 G, uint8 B, uint8 A, float* Out)
	{
		Out[0] = R / 255.0f;
		Out[1] = G / 255.0f;
		Out[2] = B / 255.0f;
		Out[3] = A / 255.0f;
	}
};
