#include "VrgSimActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* LitMatPath = TEXT("/Game/VRgrid/M_VrgInstancedLit.M_VrgInstancedLit");
	const TCHAR* EmissiveMatPath = TEXT("/Game/VRgrid/M_VrgInstanced.M_VrgInstanced");
	const TCHAR* TranslucentMatPath =
		TEXT("/Game/VRgrid/M_VrgInstancedTranslucent.M_VrgInstancedTranslucent");
	const TCHAR* CarMeshPath =
		TEXT("/ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar.SKM_SportsCar");
	// Copied into the project from the engine's template resources; there is no
	// character in Engine/Content to reference by path.
	const TCHAR* PedMeshPathA = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* PedMeshPathB = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	// Without an animation the mannequin stands in its A-pose -- arms out, like
	// a shop dummy -- which is the most obviously wrong thing in the scene.
	// A single looping walk clip fixes it with no animation blueprint.
	const TCHAR* PedWalkPath =
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd");

	constexpr double M = 100.0;   // metres -> Unreal centimetres

	// Measured-ish daylight albedos. Asphalt really is this dark (~7%
	// reflectance) -- the instinct to lighten it is what makes a render look
	// like a game, because the eye reads bright grey as plastic.
	const FLinearColor AsphaltColour(0.055f, 0.055f, 0.058f);
	const FLinearColor KerbColour(0.31f, 0.305f, 0.295f);
	const FLinearColor PavementColour(0.24f, 0.235f, 0.225f);
	const FLinearColor PaintColour(0.82f, 0.81f, 0.76f);

	// Concrete, render, brick and stone -- muted and desaturated. Saturated
	// facades are the other tell.
	const FLinearColor FacadeTones[] = {
		FLinearColor(0.42f, 0.41f, 0.38f),
		FLinearColor(0.36f, 0.34f, 0.31f),
		FLinearColor(0.48f, 0.46f, 0.43f),
		FLinearColor(0.31f, 0.28f, 0.26f),
		FLinearColor(0.44f, 0.40f, 0.35f),
		FLinearColor(0.26f, 0.26f, 0.27f),
	};
	// Glass reads as glass because it is DARK and SMOOTH, not because it glows.
	const FLinearColor GlassColour(0.045f, 0.052f, 0.062f);

	const TCHAR* PedMatPath = TEXT("/Game/VRgrid/M_VrgPed.M_VrgPed");
	const TCHAR* ScanMatPath = TEXT("/Game/VRgrid/M_VrgScan.M_VrgScan");
	const TCHAR* BandMatPath = TEXT("/Game/VRgrid/M_VrgBand.M_VrgBand");

	/** Accuracy ramp, Okabe-Ito stops so it survives all three colour-vision
	 *  deficiencies -- the same discipline `dashboard/palettes.py` follows and
	 *  `cvd.py` gates in CI. Bluish green = confident, yellow = middling,
	 *  vermillion = the map does not really know. */
	FLinearColor AccuracyRamp(float U)
	{
		const FLinearColor Good(0.0f, 0.62f, 0.45f);    // (0,158,115)
		const FLinearColor Mid(0.94f, 0.89f, 0.26f);    // (240,228,66)
		const FLinearColor Poor(0.84f, 0.37f, 0.0f);    // (213,94,0)
		return (U < 0.5f)
			? FMath::Lerp(Good, Mid, U * 2.0f)
			: FMath::Lerp(Mid, Poor, (U - 0.5f) * 2.0f);
	}

	// Slot 0 on both mannequins is M_HeadLegs and slot 1 is M_Torso, so skin
	// and clothing really are separable. Tones kept muted: a street of
	// primary-coloured shirts is the other thing that reads as a game.
	const FLinearColor SkinTones[] = {
		FLinearColor(0.62f, 0.44f, 0.33f), FLinearColor(0.48f, 0.32f, 0.22f),
		FLinearColor(0.76f, 0.58f, 0.45f), FLinearColor(0.35f, 0.23f, 0.16f),
		FLinearColor(0.68f, 0.50f, 0.38f), FLinearColor(0.55f, 0.38f, 0.27f),
	};
	const FLinearColor ClothTones[] = {
		FLinearColor(0.10f, 0.12f, 0.18f), FLinearColor(0.22f, 0.21f, 0.20f),
		FLinearColor(0.08f, 0.14f, 0.13f), FLinearColor(0.32f, 0.30f, 0.27f),
		FLinearColor(0.15f, 0.10f, 0.10f), FLinearColor(0.12f, 0.16f, 0.24f),
		FLinearColor(0.40f, 0.38f, 0.34f), FLinearColor(0.18f, 0.18f, 0.20f),
	};
}

AVrgSimActor::AVrgSimActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Surfaces = MakeIsm(TEXT("Surfaces"), /*bEmissive=*/false);
	Emissives = MakeIsm(TEXT("Emissives"), /*bEmissive=*/true);

	RoadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
	RoadMesh->SetupAttachment(Root);
	RoadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RoadMesh->SetCanEverAffectNavigation(false);
	RoadMesh->bUseAsyncCooking = true;

	BandMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BandMesh"));
	BandMesh->SetupAttachment(Root);
	BandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BandMesh->SetCanEverAffectNavigation(false);
	BandMesh->SetCastShadow(false);
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, BandMatPath))
	{
		BandMesh->SetMaterial(0, Mat);
	}

	// The map layers are UNLIT, like the Rerun view: their colour is the
	// colour the exporter wrote (the height ramp, the class palette), and
	// letting the sun multiply it would make two cells at the same elevation
	// read as different heights.
	// The cell layers are TRANSLUCENT so the street reads through them; the
	// sweep and the ring lines stay opaque, because they are thin enough not
	// to hide anything and crisper for it.
	MapOccupied = MakeMapIsm(TEXT("MapOccupied"), true);
	MapFree     = MakeMapIsm(TEXT("MapFree"), true);
	MapUnknown  = MakeMapIsm(TEXT("MapUnknown"), true);
	MapPoints   = MakeMapIsm(TEXT("MapPoints"), false);
	MapGhosts   = MakeMapIsm(TEXT("MapGhosts"), false);
	MapRings    = MakeMapIsm(TEXT("MapRings"), true);
	MapBlindCone = MakeMapIsm(TEXT("MapBlindCone"), true);
	MapSweep     = MakeMapIsm(TEXT("MapSweep"), true);
	MapObjects   = MakeMapIsm(TEXT("MapObjects"), true);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetAutoActivate(true);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);

	Car = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Car"));
	Car->SetupAttachment(Root);
	Car->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Car->SetUsingAbsoluteLocation(true);
	Car->SetUsingAbsoluteRotation(true);

	HeadlightL = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightL"));
	HeadlightR = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightR"));
	for (USpotLightComponent* S : {HeadlightL.Get(), HeadlightR.Get()})
	{
		S->SetupAttachment(Root);
		S->SetUsingAbsoluteLocation(true);
		S->SetUsingAbsoluteRotation(true);
		S->SetIntensity(150000.0f);
		S->SetAttenuationRadius(9000.0f);
		S->SetInnerConeAngle(16.0f);
		S->SetOuterConeAngle(44.0f);
		S->SetLightColor(FLinearColor(1.0f, 0.96f, 0.88f));
		S->SetCastShadows(false);   // two shadow-casting spots over a whole street is not worth it
	}

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(Root);
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(Root);
	FillLight->SetCastShadows(false);
	FillLight->SetIntensity(0.0f);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(Root);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
}

UInstancedStaticMeshComponent* AVrgSimActor::MakeIsm(FName Name, bool bEmissive)
{
	UInstancedStaticMeshComponent* Ism =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	Ism->SetupAttachment(Root);
	Ism->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CubeMeshPath));

	// The lit material takes custom data 0-2 as base colour and 3 as ROUGHNESS;
	// the emissive one takes 0-2 as emissive and 3 as alpha. Both are generated
	// by Content/Python/setup_assets.py -- and both need
	// bUsedWithInstancedStaticMeshes, or UE substitutes the default lit
	// material at runtime and the whole street renders black.
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, bEmissive ? EmissiveMatPath : LitMatPath))
	{
		Ism->SetMaterial(0, Mat);
	}
	Ism->NumCustomDataFloats = 4;
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCanEverAffectNavigation(false);
	Ism->SetCastShadow(!bEmissive);
	return Ism;
}

UInstancedStaticMeshComponent* AVrgSimActor::MakeMapIsm(FName Name, bool bTranslucent)
{
	UInstancedStaticMeshComponent* Ism =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	Ism->SetupAttachment(Root);
	// Cylinders, not cubes: a cell is a circular footprint of confidence, and
	// the disc is what carries the accuracy read. `/Engine/BasicShapes/Cylinder`
	// is 1 m across and 1 m tall at scale 1, so an XY scale of d gives a disc
	// of diameter d metres.
	// Boxes for everything that tiles a surface; the cylinder is only for the
	// blind cone, which is genuinely a disc.
	const bool bDisc = (Name == FName(TEXT("MapBlindCone")));
	Ism->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,
		bDisc ? CylinderMeshPath : CubeMeshPath));
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, bTranslucent ? TranslucentMatPath : EmissiveMatPath))
	{
		Ism->SetMaterial(0, Mat);
	}
	Ism->NumCustomDataFloats = 4;   // rgb + alpha
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCanEverAffectNavigation(false);
	Ism->SetCastShadow(false);
	Ism->bAffectDistanceFieldLighting = false;
	return Ism;
}

void AVrgSimActor::AddBox(UInstancedStaticMeshComponent* Ism, const FVector& CentreM,
                          const FVector& SizeM, const FLinearColor& Colour, float Roughness)
{
	// `/Engine/BasicShapes/Cube` is 1 m on a side at scale 1, so the scale IS
	// the size in metres.
	const FTransform T(FRotator::ZeroRotator,
	                   FVector(CentreM.X * M, CentreM.Y * M, CentreM.Z * M),
	                   SizeM);
	const int32 Index = Ism->AddInstance(T, /*bWorldSpace=*/true);
	const float Data[4] = {Colour.R, Colour.G, Colour.B, Roughness};
	Ism->SetCustomData(Index, TArrayView<const float>(Data, 4), false);
}

bool AVrgSimActor::LoadPath()
{
	PathPos.Reset();
	PathYawDeg.Reset();
	PathS.Reset();
	bHasPath = false;

	if (!bFollowRealPath || !bMapLoaded)
	{
		return false;
	}

	// The exporter's final frame carries the whole trail -- every vehicle
	// position of the run -- so the route comes from the drive itself rather
	// than being drawn by hand.
	FVrgFrame Frame;
	FString Error;
	if (!FVrgFrameReader::LoadFrame(MapScene.FinalFramePath(), Frame, Error)
	    || Frame.Trajectory.Num() < 8)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid sim: no usable trajectory (%s) -- falling back to "
		            "the straight street"), *Error);
		return false;
	}

	// ⚑ EVERY POSE, EXACTLY AS EXPORTED. No resampling, no smoothing, no
	//   invented geometry. This trail is `frame.vehicle_xyz_world` for every
	//   frame of the run -- the same values `PipelineView` logged to Rerun --
	//   so the route the car drives here IS the drive, turn for turn and
	//   metre for metre. An earlier version thinned it to a 3 m minimum
	//   spacing, which is a modification of the data however small, and it is
	//   gone.
	PathPos.Reserve(Frame.Trajectory.Num());
	for (int32 i = 0; i < Frame.Trajectory.Num(); ++i)
	{
		const FVector P = FVrgFrameReader::ToUnreal(
			Frame.Trajectory[i].X, Frame.Trajectory[i].Y, Frame.Trajectory[i].Z);
		// Drop only EXACT duplicates: a stationary frame contributes no
		// direction, and two identical points make the heading undefined.
		if (PathPos.Num() == 0 || !P.Equals(PathPos.Last(), 0.5))
		{
			PathPos.Add(P);
		}
	}
	if (PathPos.Num() < 8)
	{
		return false;
	}

	// Arc length and heading per sample.
	PathS.Reserve(PathPos.Num());
	PathYawDeg.Reserve(PathPos.Num());
	double Acc = 0.0;
	for (int32 i = 0; i < PathPos.Num(); ++i)
	{
		if (i > 0)
		{
			Acc += FVector::Dist2D(PathPos[i], PathPos[i - 1]) / 100.0;
		}
		PathS.Add(static_cast<float>(Acc));

		// Heading is read over a few metres of travel rather than between
		// adjacent poses. At ~0.8 m spacing, GPS/INS noise swings a
		// pose-to-pose bearing by tens of degrees and the street would zigzag.
		// The POSITIONS are untouched -- this only decides which way the car
		// and the kerbs face along them.
		const int32 A = FMath::Max(i - 3, 0);
		const int32 B = FMath::Min(i + 3, PathPos.Num() - 1);
		const FVector Dir = PathPos[B] - PathPos[A];
		PathYawDeg.Add(FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)));
	}
	PathLengthM = PathS.Last();
	StreetLengthM = PathLengthM;
	bHasPath = true;

	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid sim: route from the drive -- %d samples, %.0f m, "
	            "elevation %.1f m"),
	       PathPos.Num(), PathLengthM,
	       (PathPos.Num() ? (TArrayView<const FVector>(PathPos)[0].Z) : 0.0) / 100.0);
	return true;
}

bool AVrgSimActor::CrossesRouteAt(const FVector& WorldPos, float OwnS,
                                  float OwnYawDeg, float MinDistM,
                                  float IgnoreSpanM) const
{
	if (!bHasPath)
	{
		return false;
	}
	const double MinSq = static_cast<double>(MinDistM) * MinDistM * 10000.0;

	for (int32 i = 0; i < PathPos.Num(); ++i)
	{
		float DeltaS = FMath::Abs(PathS[i] - OwnS);
		if (PathLengthM > 0.0f)
		{
			DeltaS = FMath::Min(DeltaS, PathLengthM - DeltaS);
		}
		if (DeltaS < IgnoreSpanM)
		{
			continue;
		}
		if (FVector::DistSquared2D(PathPos[i], WorldPos) >= MinSq)
		{
			continue;
		}
		// Close by -- but is it a CROSSING, or the same street driven again?
		const float Diff = FMath::Abs(
			FMath::FindDeltaAngleDegrees(PathYawDeg[i], OwnYawDeg));
		if (Diff > 40.0f && Diff < 140.0f)
		{
			return true;
		}
	}
	return false;
}

bool AVrgSimActor::ClearOfRoute(const FVector& WorldPos, float OwnS,
                                float MinDistM, float IgnoreSpanM) const
{
	if (!bHasPath)
	{
		return true;
	}
	// ⚑ The ignore span has to suit what is being placed.
	//   A BUILDING sits ~20 m off the route, so its own stretch is already
	//   inside any sensible clearance and has to be ignored generously.
	//   A FOOTWAY sits 10 m off, and a wide ignore hides exactly the case it
	//   exists to catch: a hairpin, where the two legs are close in space AND
	//   close in arc length. That is why a pavement was still being laid
	//   across the carriageway.
	const double MinSq = static_cast<double>(MinDistM) * MinDistM * 10000.0;

	for (int32 i = 0; i < PathPos.Num(); ++i)
	{
		float DeltaS = FMath::Abs(PathS[i] - OwnS);
		if (PathLengthM > 0.0f)
		{
			DeltaS = FMath::Min(DeltaS, PathLengthM - DeltaS);
		}
		if (DeltaS < IgnoreSpanM)
		{
			continue;
		}
		if (FVector::DistSquared2D(PathPos[i], WorldPos) < MinSq)
		{
			return false;
		}
	}
	return true;
}

bool AVrgSimActor::RouteClearsFootprint(const FVector& Centre, float YawDeg,
                                        float WidthM, float DepthM,
                                        float MinDistM) const
{
	if (!bHasPath)
	{
		return true;
	}
	const double Cos = FMath::Cos(FMath::DegreesToRadians(-YawDeg));
	const double Sin = FMath::Sin(FMath::DegreesToRadians(-YawDeg));
	const double HalfX = WidthM * 0.5 * M;
	const double HalfY = DepthM * 0.5 * M;
	const double MinSq = static_cast<double>(MinDistM) * MinDistM * 10000.0;

	for (int32 i = 0; i < PathPos.Num(); ++i)
	{
		// Into the box's own frame, then the standard point-to-box distance.
		const double dx = PathPos[i].X - Centre.X;
		const double dy = PathPos[i].Y - Centre.Y;
		const double lx = dx * Cos - dy * Sin;
		const double ly = dx * Sin + dy * Cos;
		const double ox = FMath::Max(FMath::Abs(lx) - HalfX, 0.0);
		const double oy = FMath::Max(FMath::Abs(ly) - HalfY, 0.0);
		if (ox * ox + oy * oy < MinSq)
		{
			return false;
		}
	}
	return true;
}

void AVrgSimActor::PathAt(float AlongM, float LateralM,
                          FVector& OutPos, float& OutYawDeg) const
{
	if (!bHasPath)
	{
		// Fallback: the original straight street.
		OutYawDeg = 0.0f;
		OutPos = FVector(AlongM * M, LateralM * M, 0.0);
		return;
	}

	float S = FMath::Fmod(AlongM, PathLengthM);
	if (S < 0.0f) { S += PathLengthM; }

	// Where on the route is that? The samples are monotonic in arc length, so
	// a binary search lands the segment directly.
	int32 Lo = 0;
	int32 Hi = PathS.Num() - 1;
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (PathS[Mid] <= S) { Lo = Mid; } else { Hi = Mid; }
	}
	const float Span = FMath::Max(PathS[Hi] - PathS[Lo], 0.001f);
	const float A = FMath::Clamp((S - PathS[Lo]) / Span, 0.0f, 1.0f);

	const FVector Centre = FMath::Lerp(PathPos[Lo], PathPos[Hi], A);
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(
		FMath::Lerp(FMath::Sin(FMath::DegreesToRadians(PathYawDeg[Lo])),
		            FMath::Sin(FMath::DegreesToRadians(PathYawDeg[Hi])), A),
		FMath::Lerp(FMath::Cos(FMath::DegreesToRadians(PathYawDeg[Lo])),
		            FMath::Cos(FMath::DegreesToRadians(PathYawDeg[Hi])), A)));

	// Lateral is measured along the route's right-hand normal, which is what
	// keeps a kerb parallel to the road through a bend.
	const FVector Right = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(0, 1, 0));
	OutPos = Centre + Right * (LateralM * M);
	OutYawDeg = Yaw;
}

float AVrgSimActor::SeamOverlapM(float S, float Step, float LateralM,
                                 float WidthM) const
{
	// Two rectangles meeting at an angle leave a wedge on the outside of the
	// turn. For a strip of half-width d from the centre of the joint, the
	// outer corners separate by 2*d*tan(theta/2) -- so extending the segment
	// by that much closes it exactly, and closes it MORE where the bend is
	// tighter or the strip further from the centre line.
	FVector A, B; float YawA, YawB;
	PathAt(S, 0.0f, A, YawA);
	PathAt(S + Step, 0.0f, B, YawB);

	const float DeltaDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(YawA, YawB));
	if (DeltaDeg < 0.05f)
	{
		return 0.0f;                    // straight: nothing to close
	}
	const float HalfSpan = FMath::Abs(LateralM) + WidthM * 0.5f;
	const float Extra = 2.0f * HalfSpan
		* FMath::Tan(FMath::DegreesToRadians(FMath::Min(DeltaDeg, 80.0f)) * 0.5f);

	// A hairpin would otherwise ask for a segment tens of metres long, which
	// would be worse than the gap it closes.
	// A tighter bend legitimately needs a longer strip; the old cap of 6x
	// the step was being hit on the sharpest corners and leaving a wedge.
	return FMath::Clamp(Extra, 0.0f, Step * 14.0f);
}

void AVrgSimActor::AddBoxAt(UInstancedStaticMeshComponent* Ism,
                            const FVector& WorldPos, const FVector& SizeM,
                            float YawDeg, const FLinearColor& Colour,
                            float Roughness)
{
	const int32 Index = Ism->AddInstance(
		FTransform(FRotator(0.0, YawDeg, 0.0), WorldPos, SizeM), true);
	const float Data[4] = {Colour.R, Colour.G, Colour.B, Roughness};
	Ism->SetCustomData(Index, TArrayView<const float>(Data, 4), false);
}

void AVrgSimActor::BuildLighting()
{
	if (bDaytime)
	{
		// HIGH, and nearly along the street.
		//
		// At 42 degrees and 38 off-axis the buildings raked shadow across the
		// whole carriageway, which is why the road went black -- and worst in
		// blind-spot-only mode, where the coloured bands were no longer
		// lighting it up. Up near 70 and turned down the street, the facades
		// still catch enough angle to read as solid but the road stays lit.
		// ⚑ 56 DEGREES, AND THE NUMBER IS A COMPROMISE BETWEEN TWO FAILURES.
		//   At 42 the buildings raked shadow across the whole carriageway and
		//   the road went black. At 68 the opposite: a 1.36 m car casts 0.55 m,
		//   which from a chase camera is entirely hidden under its own
		//   bodywork -- so the cars had NO shadow at all and read as pasted
		//   onto the road. The nearest dark shape was a building's shadow
		//   several metres away, and the eye pairs the two: "the car is
		//   floating above its shadow".
		//   At 56 a car casts 0.92 m -- visible beside it, anchoring it -- and
		//   a 20 m building casts 13.5 m, just inside the 14 m carriageway, so
		//   the road still catches light down the middle.
		MoonLight->SetWorldRotation(FRotator(-56.0, 12.0, 0.0));
		MoonLight->SetIntensity(5.0f);
		MoonLight->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f));
		// ⚑ THE SHADOW HAS TO TOUCH THE CAR.
		//   350 m of cascaded shadow spread over the default three cascades
		//   makes the near cascade's texels centimetres across, and UE's
		//   depth bias then pushes the whole shadow ALONG THE LIGHT -- which
		//   at a 68 degree sun is a big sideways shift on the ground. The car
		//   ends up standing a couple of metres from its own shadow, and once
		//   you see it you cannot stop seeing it ("peter-panning").
		//
		//   150 m is more than the chase camera can see anyway (it sits 37 m
		//   back), four cascades pack the resolution into the near ground, and
		//   the contact shadow is the part that actually re-joins the wheels
		//   to the road: it ray-marches in screen space where the shadow map
		//   is too coarse to be trusted.
		// ⚑ THIS PROJECT USES VIRTUAL SHADOW MAPS (r.Shadow.Virtual.Enable=1),
		//   so the cascaded-shadow knobs are INERT here -- DynamicShadowCascades,
		//   CascadeDistributionExponent, ShadowBias, ShadowSlopeBias and
		//   DynamicShadowDistanceMovableLight all do precisely nothing. They
		//   were set here for a while to chase cars standing apart from their
		//   shadows, and changed nothing, which is the tell.
		//   The real cause was VSM's own NormalBias; it is set in
		//   Config/DefaultEngine.ini, where it belongs.
		//   Contact shadows are NOT inert under VSM and are worth keeping:
		//   they are screen-space, so they hold the contact point together at
		//   exactly the scale a shadow map is worst at.
		MoonLight->ContactShadowLength = 0.35f;
		MoonLight->ContactShadowLengthInWS = false;
		MoonLight->SetCastShadows(true);

		// ⚑ AMBIENT HAS TO CARRY THE SHADOWED SIDE.
		//   At 0.75 anything the sun did not reach crushed to pure black, and
		//   this route loops, so it regularly runs past the BACK of a terrace
		//   -- a wall with no windows on it, in shadow, reading as a void
		//   across the street rather than as a building. Auto-exposure keyed
		//   to the sunlit road made it worse. A real overcast-ish sky term
		//   keeps those faces as surfaces.
		// The sky light is a captured scene, and on this map the capture does
		// not actually reach the faces the sun misses -- raising it from 0.75
		// to 2.6 changed nothing on screen, which is how we know. The fill
		// light does the job the sky light was supposed to: shadowless, from
		// behind and opposite the sun, just enough that a wall the sun cannot
		// see still reads as a wall.
		FillLight->SetWorldRotation(FRotator(-38.0, 192.0, 0.0));
		// At 56 degrees the buildings put most of the carriageway in shadow for
		// long stretches, and at 1.6 the shadowed road read as a featureless
		// slab -- no kerb, no markings, nothing to tell road from footway.
		// The fill is what makes the shadowed side of the street legible.
		FillLight->SetIntensity(2.6f);
		FillLight->SetLightColor(FLinearColor(0.74f, 0.82f, 1.0f));
		FillLight->SetCastShadows(false);

		SkyLight->SetIntensity(2.6f);
		SkyLight->SetLightColor(FLinearColor(0.88f, 0.93f, 1.0f));
		SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
		SkyLight->bRealTimeCapture = true;

		// Light haze only, for aerial perspective down the street. Heavy fog
		// is a night trick and in daylight it just looks like a filter. Enough
		// of it that a block at ninety metres sits back rather than silhouettes.
		Fog->SetFogDensity(0.0065f);
		Fog->SetFogHeightFalloff(0.18f);
		Fog->SetFogInscatteringColor(FLinearColor(0.42f, 0.50f, 0.62f));
		Fog->SetVolumetricFog(false);
		return;
	}

	MoonLight->SetWorldRotation(FRotator(-14.0, 145.0, 0.0));
	MoonLight->SetIntensity(1.4f);
	MoonLight->SetLightColor(FLinearColor(0.42f, 0.52f, 0.78f));
	MoonLight->SetDynamicShadowDistanceMovableLight(20000.0f);

	SkyLight->SetIntensity(0.85f);
	SkyLight->SetLightColor(FLinearColor(0.35f, 0.42f, 0.62f));
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;

	Fog->SetFogDensity(0.018f);
	Fog->SetFogHeightFalloff(0.12f);
	Fog->SetFogInscatteringColor(FLinearColor(0.055f, 0.070f, 0.115f));
	Fog->SetVolumetricFog(true);
	Fog->SetVolumetricFogExtinctionScale(1.4f);
	Fog->SetVolumetricFogDistance(18000.0f);
}

void AVrgSimActor::AddRibbon(int32 Section, float LatA, float LatB,
                             float ZOffsetM, const FLinearColor& Colour,
                             float Roughness, bool bStopAtJunctions)
{
	if (RoadMesh == nullptr || !bHasPath || PathPos.Num() < 2)
	{
		return;
	}

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<FColor> Colors;
	TArray<bool> Cut;

	const float HalfRoad = RoadWidthM * 0.5f;
	const FVector Up(0, 0, ZOffsetM * M);

	// ⚑ THE JUNCTION TEST IS MEASURED IN CARRIAGEWAY WIDTHS.
	//   It briefly used the ribbon's own outer edge, on the theory that a
	//   wider strip can overhang more. True, and still wrong: at a 90 m reach
	//   something is in range of a 3.7 km loop EVERYWHERE, so the ground
	//   strips collapsed over 100% of the route and the only ground left was
	//   the flat backstop plane 15 m below. Every building, car and footway
	//   then stood over a void.
	//   What a footway stops for is a road crossing it, and that is one
	//   carriageway wide however wide the footway is.
	const float JunctionReach = RoadWidthM * 0.5f + 4.0f;

	// One pair of vertices per route sample. The strip therefore bends exactly
	// where the drive bent, with no joints to mitre and nothing to overlap.
	int32 Ring = 0;
	float Travelled = 0.0f;
	for (int32 i = 0; i < PathPos.Num(); ++i)
	{
		const float S = PathS[i];

		// ⚑ A WIDE STRIP FOLDS THROUGH ITSELF ON A TIGHT BEND.
		//   Offsetting sideways from the centreline is only well behaved while
		//   the offset stays inside the turn's radius. Past that, the inner
		//   edge crosses the centre of curvature and comes out the far side --
		//   consecutive samples end up in reverse order and the strip stands
		//   up as a crumpled vertical wall. At ninety metres the ground apron
		//   did exactly that on the sharper corners: a black slab across the
		//   street. Clamping to a fraction of the local radius narrows the
		//   strip through a bend and leaves it full width everywhere else.
		float LimA = LatA;
		float LimB = LatB;
		{
			const int32 P = FMath::Max(i - 2, 0);
			const int32 Q = FMath::Min(i + 2, PathPos.Num() - 1);
			const float DS = PathS[Q] - PathS[P];
			if (DS > 0.01f)
			{
				const float DYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(
					PathYawDeg[P], PathYawDeg[Q]));
				const float Kappa = FMath::DegreesToRadians(DYaw) / DS;
				if (Kappa > 1e-4f)
				{
					const float MaxLat = 0.75f / Kappa;
					LimA = FMath::Clamp(LatA, -MaxLat, MaxLat);
					LimB = FMath::Clamp(LatB, -MaxLat, MaxLat);
				}
			}
		}

		FVector A, B; float Yaw;
		PathAt(S, LimA, A, Yaw);
		PathAt(S, LimB, B, Yaw);

		// ⚑ A CLEAN BREAK, NOT A TAPER.
		//   A footway stops where another part of the drive crosses it. This
		//   used to collapse both edges onto the strip's midpoint, leaving a
		//   degenerate sliver running through the junction and joining it to
		//   the full-width rings either side with long thin triangles. Six
		//   strips doing that at once is most of what reads as "scattered".
		//   Marking the sample CUT and not emitting the quad gives a pavement
		//   that ends at the kerb line and resumes the other side, which is
		//   what a dropped kerb looks like.
		bool bCut = false;
		if (bStopAtJunctions)
		{
			FVector Centre; float CYaw;
			PathAt(S, 0.0f, Centre, CYaw);
			bCut = CrossesRouteAt(Centre, S, CYaw, JunctionReach, 16.0f);
		}
		Cut.Add(bCut);

		const FVector Lift = Up + FVector(0, 0, ZRampAt(S));
		Verts.Add(A + Lift);
		Verts.Add(B + Lift);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		if (i > 0)
		{
			Travelled += (PathS[i] - PathS[i - 1]);
		}
		UVs.Add(FVector2D(Travelled * 0.2f, 0.0f));
		UVs.Add(FVector2D(Travelled * 0.2f, 1.0f));
		Tangents.Add(FProcMeshTangent(1, 0, 0));
		Tangents.Add(FProcMeshTangent(1, 0, 0));
		Colors.Add(FColor::White);
		Colors.Add(FColor::White);

		if (i > 0 && !Cut[i] && !Cut[i - 1])
		{
			// ⚑ WINDING. Face UP, not down.
			//   Wound the other way the whole road surface points at the
			//   ground: back-face culled from above, so you see straight
			//   through it to the flat plane metres below, and the shadow pass
			//   has nothing to stop the light either -- so every shadow in the
			//   scene lands down THERE instead of on the road. A car driving
			//   on an invisible surface above a plain floor that holds all the
			//   shadows is exactly what that looks like.
			const int32 P0 = (i - 1) * 2;
			Tris.Add(P0);     Tris.Add(P0 + 1); Tris.Add(P0 + 2);
			Tris.Add(P0 + 1); Tris.Add(P0 + 3); Tris.Add(P0 + 2);
		}
		++Ring;
	}

	RoadMesh->CreateMeshSection(Section, Verts, Tris, Normals, UVs, Colors,
	                            Tangents, /*bCreateCollision=*/false);

	// `-VrgRibbonDebug`: one flat colour per strip, so which surface is where
	// stops being a question of reading tones off a screenshot.
	FLinearColor Use = Colour;
	if (FParse::Param(FCommandLine::Get(), TEXT("VrgRibbonDebug")))
	{
		static const FLinearColor Key[11] = {
			FLinearColor(1.0f, 0.0f, 0.0f),   // 0  carriageway  RED
			FLinearColor(1.0f, 0.5f, 0.0f),   // 1  kerb L       ORANGE
			FLinearColor(1.0f, 0.5f, 0.0f),   // 2  kerb R       ORANGE
			FLinearColor(0.0f, 0.4f, 1.0f),   // 3  footway L    BLUE
			FLinearColor(0.0f, 0.4f, 1.0f),   // 4  footway R    BLUE
			FLinearColor(1.0f, 1.0f, 1.0f),   // 5  edge line L  WHITE
			FLinearColor(1.0f, 1.0f, 1.0f),   // 6  edge line R  WHITE
			FLinearColor(0.0f, 1.0f, 0.0f),   // 7  verge L      GREEN
			FLinearColor(0.0f, 1.0f, 0.0f),   // 8  verge R      GREEN
			FLinearColor(1.0f, 0.0f, 1.0f),   // 9  apron L      MAGENTA
			FLinearColor(1.0f, 0.0f, 1.0f),   // 10 apron R      MAGENTA
		};
		Use = Key[FMath::Clamp(Section, 0, 10)];
	}

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, PedMatPath);
	UMaterialInstanceDynamic* MID =
		Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (MID != nullptr)
	{
		MID->SetVectorParameterValue(TEXT("Tint"), Use);
		MID->SetScalarParameterValue(TEXT("Rough"), Roughness);
		RoadMesh->SetMaterial(Section, MID);
	}

	if (MID == nullptr)
	{
		// Loud: without it every strip renders in the material's default grey
		// and the carriageway cannot be told from the footway.
		UE_LOG(LogTemp, Error,
		       TEXT("VRgrid sim: ribbon %d could not tint -- is %s missing? "
		            "Run: demo.ps1 materials"), Section, PedMatPath);
	}
	else if (bDiagnostics)
	{
		UE_LOG(LogTemp, Log, TEXT("VRgrid ribbon %d: %d verts, tint (%.3f %.3f %.3f)"),
		       Section, Verts.Num(), Colour.R, Colour.G, Colour.B);
	}
}

void AVrgSimActor::BuildRoadRibbons()
{
	const float HalfRoad = RoadWidthM * 0.5f;

	// ⚑ The footway runs UNDER the building line, not up to it. At 5.3 m it
	//   ended at 12.3 m while the frontage begins at 12.6, and that 30 cm of
	//   bare ground ran the length of every street. 6.4 m tucks it beneath the
	//   plinth, so the join is hidden rather than merely narrow.
	//  section  what                       lateral span            height
	AddRibbon(0, -HalfRoad,        HalfRoad,        0.00f, AsphaltColour,  0.46f, false);
	AddRibbon(1, -HalfRoad - 0.30f, -HalfRoad,      0.14f, KerbColour,     0.75f, true);
	AddRibbon(2,  HalfRoad,         HalfRoad + 0.30f, 0.14f, KerbColour,   0.75f, true);
	AddRibbon(3, -HalfRoad - 6.40f, -HalfRoad - 0.30f, 0.13f, PavementColour, 0.85f, true);
	AddRibbon(4,  HalfRoad + 0.30f, HalfRoad + 6.40f, 0.13f, PavementColour, 0.85f, true);
	// Edge lines, as their own thin ribbons rather than a row of dashes.
	AddRibbon(5, -HalfRoad + 0.22f, -HalfRoad + 0.36f, 0.012f, PaintColour * 0.45f, 1.0f, true);
	AddRibbon(6,  HalfRoad - 0.36f,  HalfRoad - 0.22f, 0.012f, PaintColour * 0.45f, 1.0f, true);

	// ⚑ THE VERGE HAS TO CLIMB WITH THE ROAD.
	//   Replacing the box street with ribbons took the ground strip with it,
	//   and the only ground left was one flat plane under the LOWEST point of
	//   the drive. This route rises about fifteen metres, so for most of it
	//   the footway would have ended in a cliff. These two strips follow the
	//   elevation exactly like the carriageway does; the flat plane stays as
	//   the backstop for whatever is further out than thirty metres, by which
	//   point the frontage hides it.
	const FLinearColor GroundColour(0.14f, 0.135f, 0.125f);
	//   bStopAtJunctions is FALSE for ground: a junction still has ground on
	//   it, and the turn-radius clamp is what stops a wide strip folding.
	//
	//   ⚑ AND GROUND SITS BELOW THE CARRIAGEWAY. This route re-drives its own
	//     streets, so a 30 m verge (never mind a 90 m apron) from one pass
	//     lies straight across the road of another. Culling that away is what
	//     left nothing under the buildings; ordering it away costs nothing.
	//     Ground under road under footway is the real arrangement anyway, and
	//     it makes every overlap resolve the right way round without a test.
	AddRibbon(7, -30.0f,           -HalfRoad - 6.40f, -0.05f, GroundColour, 0.92f, false);
	AddRibbon(8,  HalfRoad + 6.40f, 30.0f,            -0.05f, GroundColour, 0.92f, false);

	// ⚑ NO APRON. It used to run out to ninety metres, and it had to go.
	//   It was added because sky was showing at street level -- but that was
	//   the ROAD being invisible (its triangles faced down), not a gap in the
	//   ground. With the frontage actually rendering, the 30 m verge plus the
	//   buildings cover the horizon on their own.
	//   And it could never have worked here: this route re-drives its own
	//   streets, so a 90 m ground strip from one pass lay flat across the
	//   carriageway of another. Ordering ground below road fixes that only
	//   within one stretch; across two stretches at different heights the
	//   higher one's ground still buries the lower one's road.

	UE_LOG(LogTemp, Log, TEXT("VRgrid sim: road built as %d continuous ribbons "
	                          "over %d route samples"), 9, PathPos.Num());
}

void AVrgSimActor::BuildStreet()
{
	FRandomStream Rng(RandomSeed + 3);
	const float HalfRoad = RoadWidthM * 0.5f;
	const float Step = FMath::Max(RoadSegmentM, 0.5f);
	const int32 N = FMath::Max(2, FMath::CeilToInt(StreetLengthM / Step));

	// ⚑ ONE GROUND PLANE UNDER THE WHOLE DRIVE.
	//   Gaps in the frontage were showing SKY at street level, because the
	//   only ground was a 30 m ribbon along the route and past that there was
	//   nothing. A single plane under the lowest point of the drive cannot
	//   overhang anything -- which is what went wrong when the ribbon was
	//   widened instead -- and turns every hole into ground rather than sky.
	if (bHasPath && PathPos.Num() > 0)
	{
		FVector Min = PathPos[0];
		FVector Max = PathPos[0];
		for (const FVector& P : PathPos)
		{
			Min = Min.ComponentMin(P);
			Max = Max.ComponentMax(P);
		}
		const FVector Centre((Min.X + Max.X) * 0.5, (Min.Y + Max.Y) * 0.5, 0.0);
		const double SizeX = (Max.X - Min.X) / M + 400.0;
		const double SizeY = (Max.Y - Min.Y) / M + 400.0;
		AddBoxAt(Surfaces, FVector(Centre.X, Centre.Y, Min.Z - 1.2 * M),
		         FVector(SizeX, SizeY, 1.0f), 0.0f,
		         FLinearColor(0.115f, 0.112f, 0.105f), 0.95f);
	}

	// ⚑ THE CARRIAGEWAY IS ONE COLOUR.
	//   Every slab used to get its own random shade, which is precisely what
	//   made the road read as a row of rectangles: the joints were invisible
	//   until the tone either side of them differed. Wear now comes only from
	//   the resurfacing patches below, which are SUPPOSED to be seen as
	//   patches.
	BuildRoadRibbons();

	// Centre line: 3 m dashes on a 9 m pitch, some worn away entirely. Drawn
	// in short pieces so a dash follows a bend instead of cutting the corner.
	for (float S = 6.0f; S < StreetLengthM; S += 9.0f)
	{
		if (Rng.FRand() < 0.06f * Grit)
		{
			continue;
		}
		const float Wear = 1.0f - Rng.FRandRange(0.0f, 0.55f) * Grit;
		const float DashLen = 3.0f - Rng.FRandRange(0.0f, 0.5f) * Grit;
		constexpr int32 Pieces = 3;
		for (int32 k = 0; k < Pieces; ++k)
		{
			const float Ds = S + (k - 1.0f) * (DashLen / Pieces);
			FVector Pos; float Yaw;
			PathAt(Ds, 0.0f, Pos, Yaw);
			AddBoxAt(Emissives, Pos + FVector(0, 0, 0.04 * M + ZRampAt(Ds)),
			         FVector(DashLen / Pieces + 0.06f, 0.16f, 0.02f), Yaw,
			         PaintColour * 0.45f * Wear, 1.0f);
		}
	}

	// Resurfacing patches and manholes -- the only intentional variation in
	// the carriageway.
	const int32 Patches = FMath::RoundToInt(N * 0.12f * Grit);
	for (int32 i = 0; i < Patches; ++i)
	{
		const float S = Rng.FRandRange(0.0f, StreetLengthM);
		const float Lat = Rng.FRandRange(-HalfRoad + 1.0f, HalfRoad - 1.0f);
		const bool bManhole = Rng.FRand() > 0.72f;
		FVector Pos; float Yaw;
		PathAt(S, Lat, Pos, Yaw);
		// ⚑ FLUSH WITH THE ROAD.
		//   `SizeM` is the SCALE on a 1 m cube, so 0.20 was a box spanning
		//   +/-10 cm about its centre -- a resurfacing patch standing 11 cm
		//   proud of the carriageway, with its own edges and its own drop
		//   shadow. They read as slabs dropped on the road rather than as part
		//   of it. 0.02 is a 2 cm lip, which is what a real patch has.
		AddBoxAt(Surfaces, Pos + FVector(0, 0, 0.008 * M + ZRampAt(S)),
		         FVector(bManhole ? 0.8f : Rng.FRandRange(1.4f, 4.5f),
		                 bManhole ? 0.8f : Rng.FRandRange(0.9f, 2.6f), 0.02f),
		         Yaw, AsphaltColour * (bManhole ? 0.8f : Rng.FRandRange(1.25f, 1.9f)),
		         bManhole ? 0.35f : 0.55f);
	}
}

void AVrgSimActor::AddFacadeAt(const FVector& FacePos, float Yaw, float Width,
                               float Height, float Side, FRandomStream& Rng)
{
	// A flat box with rectangles painted on it is the classic game building.
	// What makes a facade read as architecture is DEPTH -- a plinth, a
	// parapet, floor bands, and windows genuinely recessed so the sun casts a
	// shadow into each opening. Still boxes; it is the relief that works.
	const float FloorH = 3.4f;
	const int32 Floors = FMath::Max(2, FMath::FloorToInt((Height - 1.2f) / FloorH));
	const float Inset = 0.22f;
	const float PlinthH = FMath::Min(Height * 0.28f, FloorH * 1.2f);
	const FVector Out = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(0, 1, 0)) * (Side * M);

	AddBoxAt(Surfaces, FacePos + FVector(0, 0, (PlinthH * 0.5 - 0.5) * M),
	         FVector(Width + 0.30f, 0.5f, PlinthH + 1.0f), Yaw,
	         FLinearColor(0.24f, 0.23f, 0.22f), 0.80f);
	AddBoxAt(Surfaces, FacePos + FVector(0, 0, (Height + 0.35) * M),
	         FVector(Width + 0.42f, 0.7f, 0.70f), Yaw,
	         FLinearColor(0.30f, 0.29f, 0.28f), 0.85f);

	for (int32 f = 1; f < Floors; ++f)
	{
		const float Z = PlinthH + f * FloorH;
		if (Z > Height - 0.5f) { break; }
		AddBoxAt(Surfaces, FacePos + Out * 0.08 + FVector(0, 0, Z * M),
		         FVector(Width + 0.16f, 0.22f, 0.18f), Yaw,
		         FLinearColor(0.34f, 0.33f, 0.31f), 0.85f);
	}

	const int32 Cols = FMath::Max(2, FMath::FloorToInt(Width / 2.6f));
	const float ColPitch = Width / Cols;
	const float WinW = FMath::Min(1.5f, ColPitch * 0.62f);
	const FVector Along = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(1, 0, 0)) * M;

	for (int32 f = 0; f < Floors; ++f)
	{
		const float Z = PlinthH + f * FloorH + FloorH * 0.5f;
		if (Z > Height - 1.0f) { break; }
		for (int32 col = 0; col < Cols; ++col)
		{
			const double Offset = -Width * 0.5 + (col + 0.5) * ColPitch;
			const FVector At = FacePos + Along * Offset + FVector(0, 0, Z * M);
			AddBoxAt(Surfaces, At + Out * (Inset * 0.5),
			         FVector(WinW + 0.14f, Inset, 1.95f), Yaw,
			         FLinearColor(0.20f, 0.19f, 0.18f), 0.88f);
			AddBoxAt(Surfaces, At + Out * Inset,
			         FVector(WinW, 0.06f, 1.75f), Yaw, GlassColour,
			         0.07f + Rng.FRandRange(0.0f, 0.05f));

			if (!bDaytime && Rng.FRand() < 0.45f)
			{
				const float Warm = Rng.FRandRange(0.5f, 0.95f);
				AddBoxAt(Emissives, At + Out * (Inset + 0.03),
				         FVector(WinW * 0.94f, 0.04f, 1.70f), Yaw,
				         FLinearColor(Warm, Warm * 0.85f, Warm * 0.58f), 1.0f);
			}
		}
	}
}

void AVrgSimActor::BuildBuildings()
{
	FRandomStream Rng(RandomSeed);
	const float HalfRoad = RoadWidthM * 0.5f;
	const float Setback = HalfRoad + 5.6f;
	const float Spacing = FMath::Max(BuildingSpacingM, 8.0f);
	const int32 N = FMath::Max(1, FMath::FloorToInt(StreetLengthM / Spacing));

	for (int32 b = 0; b < N; ++b)
	{
		const float S = b * Spacing + Spacing * 0.5f;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			// Gap sites: a terrace with no missing teeth looks extruded.
			if (Rng.FRand() < 0.06f * Grit)
			{
				continue;
			}
			float Depth = Rng.FRandRange(14.0f, 26.0f);
			const float Width = Spacing - Rng.FRandRange(4.0f, 9.0f);
			const float Height = Rng.FRandRange(11.0f, 42.0f);
			float Lat = Side * (Setback + Depth * 0.5f
			                    + Rng.FRandRange(-1.2f, 2.4f) * Grit);

			FVector Pos; float Yaw;
			PathAt(S, Lat, Pos, Yaw);

			// ⚑ The route passes near itself, and it also curls back on
			//   itself inside tight bends. Measured to the FOOTPRINT with no
			//   arc-length exemption, so a building has to be clear of the
			//   whole drive -- which is the only version of this test that
			//   cannot put a wall in the middle of the road.
			// ⚑ SHRINK BEFORE GIVING UP.
			//   Dropping every building that fouled the route left holes in
			//   the frontage wherever the drive ran close to itself, and a
			//   terrace with missing teeth is worse than a shallow one. Try a
			//   shallower block set further back; only skip if even the
			//   smallest will not fit.
			bool bFits = false;
			for (int32 Attempt = 0; Attempt < 5 && !bFits; ++Attempt)
			{
				if (RouteClearsFootprint(Pos, Yaw, Width, Depth,
				                         RoadWidthM * 0.5f + 2.5f))
				{
					bFits = true;
					break;
				}
				Depth = FMath::Max(Depth * 0.62f, 5.0f);
				Lat = Side * (Setback + Depth * 0.5f + 3.0f * (Attempt + 1));
				PathAt(S, Lat, Pos, Yaw);
			}
			if (!bFits)
			{
				continue;
			}

			// Its own ground pad, at its own height on the route. Cheaper and
			// far safer than one slab wide enough to reach everything.
			if (!FParse::Param(FCommandLine::Get(), TEXT("VrgNoPads")))
			{
			// ⚑ KEEP THE PAD UNDER THE VERGE.
			//   `SizeM` is a scale on a 1 m cube, so 0.42 about -0.23 put the
			//   pad's top at -0.02 m -- 3 cm ABOVE the verge at -0.05, which
			//   is why odd pale rectangles kept surfacing along the pavement
			//   edge. Sunk to -0.30 the top lands at -0.09 and it does what a
			//   footing should: hold the building up and never be seen.
			AddBoxAt(Surfaces, Pos + FVector(0, 0, -0.30 * M),
			         FVector(Width + 6.0f, Depth + 6.0f, 0.42f), Yaw,
			         FLinearColor(0.14f, 0.135f, 0.125f), 0.92f);
			}

			const FLinearColor Tone =
				FacadeTones[Rng.RandRange(0, UE_ARRAY_COUNT(FacadeTones) - 1)];
			constexpr float BuryM = 1.0f;
			AddBoxAt(Surfaces, Pos + FVector(0, 0, (Height * 0.5 - BuryM * 0.5) * M),
			         FVector(Width, Depth, Height + BuryM), Yaw,
			         Tone * (1.0f + Rng.FRandRange(-0.12f, 0.12f) * Grit),
			         Rng.FRandRange(0.72f, 0.90f));

			FVector FacePos; float FaceYaw;
			PathAt(S, Lat - Side * (Depth * 0.5f + 0.06f), FacePos, FaceYaw);
			AddFacadeAt(FacePos, FaceYaw, Width, Height, -Side, Rng);

			// ⚑ AND THE BACK, WHERE THE DRIVE COMES ROUND BEHIND IT.
			//   "Nobody sees the back of the block" holds for a straight
			//   street and fails for a route that loops, which this one does:
			//   the far side of a terrace ends up facing a later stretch, and
			//   a twenty-metre box with nothing on it reads as a slab across
			//   the street rather than as a building. Only built where the
			//   route actually passes behind, so most blocks still cost one
			//   facade.
			FVector BackPos; float BackYaw;
			PathAt(S, Lat + Side * (Depth * 0.5f + 0.06f), BackPos, BackYaw);
			if (!ClearOfRoute(BackPos, S, 55.0f, 45.0f))
			{
				AddFacadeAt(BackPos, BackYaw, Width, Height, Side, Rng);
			}
		}
	}
}

void AVrgSimActor::BuildStreetFurniture()
{
	const float HalfRoad = RoadWidthM * 0.5f;
	int32 Lamp = 0;
	for (float S = 25.0f; S < StreetLengthM; S += 35.0f, ++Lamp)
	{
		const float Side = (Lamp % 2 == 0) ? 1.0f : -1.0f;
		const float Lat = Side * (HalfRoad + 0.9f);
		FVector Pos; float Yaw;
		PathAt(S, Lat, Pos, Yaw);

		if (!ClearOfRoute(Pos, S, RoadWidthM * 0.5f + 1.0f, 20.0f))
		{
			continue;
		}
		AddBoxAt(Surfaces, Pos + FVector(0, 0, 3.8 * M),
		         FVector(0.18f, 0.18f, 8.4f), Yaw,
		         FLinearColor(0.10f, 0.10f, 0.11f), 0.5f);

		FVector Head; float HeadYaw;
		PathAt(S, Lat - Side * 1.7f, Head, HeadYaw);
		AddBoxAt(Surfaces, Head + FVector(0, 0, 7.9 * M),
		         FVector(0.14f, 1.9f, 0.14f), HeadYaw,
		         FLinearColor(0.10f, 0.10f, 0.11f), 0.5f);
		AddBoxAt(Emissives, Head + FVector(0, 0, 7.75 * M),
		         FVector(0.55f, 0.30f, 0.14f), HeadYaw,
		         FLinearColor(1.0f, 0.80f, 0.52f), 1.0f);

		// One lamp in nine is out -- the kind of detail that stops a street
		// reading as a product shot.
		if ((Lamp % 9 == 4) && Grit > 0.3f)
		{
			continue;
		}
		UPointLightComponent* P = NewObject<UPointLightComponent>(this);
		P->SetupAttachment(Root);
		P->RegisterComponent();
		P->SetWorldLocation(Head + FVector(0, 0, 7.6 * M));
		P->SetIntensity(52000.0f);
		P->SetAttenuationRadius(3400.0f);
		P->SetLightColor(FLinearColor(1.0f, 0.78f, 0.50f));
		P->SetCastShadows(false);
		P->SetVisibility(!bDaytime);
		StreetLamps.Add(P);
	}
}

void AVrgSimActor::SpawnTraffic()
{
	FRandomStream Rng(RandomSeed + 7);
	const float LaneY = EgoLaneM;

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, CarMeshPath);
	if (Mesh == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("VRgrid sim: no car mesh, street will be empty"));
		return;
	}

	// Parked along the kerbs, plus movers in both directions. The movers are
	// the point: the other window's story is ghost removal behind moving
	// vehicles, and a static street beside it would undercut that.
	struct FSpawn { float Along; float Lateral; float Speed; };
	TArray<FSpawn> Spawns;

	// ⚑ Parked cars sat at HalfRoad - 1.5, which puts their body at 4.55-6.45 m
	//   from the centre line. The running lane is 2.55-4.45 and the ego weaves
	//   +-0.55 on top of that, so the ego clipped straight through the parked
	//   row. They are tucked against the kerb now and the weave is halved, so
	//   the two never share space.
	for (float X = 18.0f; X < StreetLengthM; X += static_cast<float>(Rng.FRandRange(22.0f, 46.0f)))
	{
		const float Side = Rng.FRand() > 0.5f ? 1.0f : -1.0f;
		const float Lat = Side * (RoadWidthM * 0.5f - 1.05f);

		// ⚑ NOT IN SOMEBODY ELSE'S CARRIAGEWAY.
		//   A car parked at the kerb of one stretch lands in the middle of
		//   another wherever the drive runs close to itself -- a car stopped
		//   across a live lane, which is the first thing the eye picks out.
		//   Its own stretch is exempt (it is meant to be parked beside that
		//   one); everything else has to stay clear.
		FVector Pos; float Yaw;
		PathAt(X, Lat, Pos, Yaw);
		if (!ClearOfRoute(Pos, X, 6.0f, 25.0f))
		{
			continue;
		}
		Spawns.Add({X, Lat, 0.0f});
	}
	for (int32 i = 0; i < MovingCars; ++i)
	{
		const bool bOncoming = (i % 2 == 0);
		const float Along = static_cast<float>(Rng.FRandRange(0.0f, StreetLengthM));
		const float Speed = bOncoming
			? -static_cast<float>(Rng.FRandRange(9.0f, 15.0f))
			:  static_cast<float>(Rng.FRandRange(7.0f, 12.0f));
		Spawns.Add({Along, bOncoming ? -LaneY : LaneY, Speed});
	}

	for (const FSpawn& S : Spawns)
	{
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this);
		Comp->SetupAttachment(Root);
		Comp->RegisterComponent();
		Comp->SetSkeletalMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		// No anim blueprint and no per-frame refresh: these are parked or
		// rolling props, and skinning 20 cars every tick for a rest pose is
		// frame time spent on nothing.
		Comp->SetComponentTickEnabled(false);
		Comp->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

		FTraffic T;
		T.Index = TrafficCars.Add(Comp);
		T.AlongM = S.Along;
		T.LateralM = S.Lateral;
		T.SpeedMS = S.Speed;
		T.SizeM = FVector(1.0f);
		Traffic.Add(T);
	}
}

int32 AVrgSimActor::CarAhead(float AlongM, float LateralM, float& OutGapM) const
{
	// Same lane means within 1.6 m laterally -- wide enough to catch a car
	// drifting in its lane, narrow enough to ignore oncoming traffic and the
	// parked row at the kerb.
	int32 Best = -1;
	OutGapM = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Traffic.Num(); ++i)
	{
		const FTraffic& T = Traffic[i];
		if (FMath::Abs(T.LateralM - LateralM) > 1.6f)
		{
			continue;
		}
		float Gap = T.AlongM - AlongM;
		// The street wraps, so "ahead" has to wrap with it.
		if (Gap < 0.0f) { Gap += StreetLengthM; }
		if (Gap > 0.5f && Gap < OutGapM)
		{
			OutGapM = Gap;
			Best = i;
		}
	}
	return Best;
}

void AVrgSimActor::SpawnPedestrians()
{
	FRandomStream Rng(RandomSeed + 19);
	USkeletalMesh* A = LoadObject<USkeletalMesh>(nullptr, PedMeshPathA);
	USkeletalMesh* B = LoadObject<USkeletalMesh>(nullptr, PedMeshPathB);
	UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, PedWalkPath);
	UMaterialInterface* PedMat = LoadObject<UMaterialInterface>(nullptr, PedMatPath);
	UE_LOG(LogTemp, Log, TEXT("VRgrid sim: ped material %s -> %s"),
	       PedMatPath, PedMat ? TEXT("loaded") : TEXT("NULL (people will be default white)"));
	if (A == nullptr && B == nullptr)
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid sim: no pedestrian mesh at %s -- street will have no people"),
		       PedMeshPathA);
		return;
	}

	const float PaveY = RoadWidthM * 0.5f + 2.8f;
	for (int32 i = 0; i < PedestrianCount; ++i)
	{
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this);
		Comp->SetupAttachment(Root);
		Comp->RegisterComponent();
		Comp->SetSkeletalMesh((Rng.FRand() > 0.5f && B) ? B : (A ? A : B));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		if (Walk != nullptr)
		{
			Comp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Comp->SetAnimation(Walk);
			Comp->SetPlayRate(Rng.FRandRange(0.85f, 1.15f));
			Comp->Play(true);
			// Offset each walk cycle, or 26 people march in lockstep.
			Comp->SetPosition(Rng.FRandRange(0.0f, 1.0f), false);
		}
		// The mannequin stands 1.8 m at scale 1 with its origin at the feet,
		// which is what the pavement height expects.
		Comp->SetWorldScale3D(FVector(Rng.FRandRange(0.92f, 1.06f)));

		// Skin on slot 0, clothing on slot 1. Overriding the mesh's own
		// materials also avoids Manny's, which point at MI_Manny_01 while the
		// engine ships only MI_Manny_01_New.
		if (PedMat != nullptr)
		{
			const FLinearColor Skin =
				SkinTones[Rng.RandRange(0, UE_ARRAY_COUNT(SkinTones) - 1)];
			const FLinearColor Cloth =
				ClothTones[Rng.RandRange(0, UE_ARRAY_COUNT(ClothTones) - 1)];

			UMaterialInstanceDynamic* MSkin = UMaterialInstanceDynamic::Create(PedMat, this);
			MSkin->SetVectorParameterValue(TEXT("Tint"), Skin);
			MSkin->SetScalarParameterValue(TEXT("Rough"), 0.62f);
			Comp->SetMaterial(0, MSkin);

			UMaterialInstanceDynamic* MCloth = UMaterialInstanceDynamic::Create(PedMat, this);
			MCloth->SetVectorParameterValue(TEXT("Tint"), Cloth);
			MCloth->SetScalarParameterValue(TEXT("Rough"), 0.88f);
			Comp->SetMaterial(1, MCloth);
		}

		FPed P;
		P.Index = Pedestrians.Add(Comp);
		P.AlongM = Rng.FRandRange(0.0f, StreetLengthM);
		P.LateralM = (Rng.FRand() > 0.5f ? 1.0f : -1.0f)
			* (PaveY + Rng.FRandRange(-1.6f, 1.6f));
		P.SpeedMS = Rng.FRandRange(0.9f, 1.6f) * (Rng.FRand() > 0.5f ? 1.0f : -1.0f);
		P.Phase = Rng.FRandRange(0.0f, 6.28f);
		// A few step off the kerb and cross. This is the shot the other window
		// is about: a person moving across the lane the car is in. They start
		// waiting on the pavement, staggered, so they do not all step out at
		// once in the first second.
		if (i % 7 == 0)
		{
			P.DwellS = Rng.FRandRange(2.0f, 30.0f);
		}
		Peds.Add(P);
	}

	// Reserve the last one for the scheduled crossing. It has no dwell and no
	// ambient crossing of its own -- it only moves when the script says so.
	if (Peds.Num() > 0)
	{
		ScriptedPedIndex = Peds.Num() - 1;
		Peds[ScriptedPedIndex].CrossTarget = 0.0f;
		Peds[ScriptedPedIndex].DwellS = 0.0f;
	}
	// Armed from the start: the crossing fires when the car comes within
	// the lead distance of ScriptedCrossAtM, wherever on the lap that is.
	bScriptedArmed = true;
}

FVector AVrgSimActor::MapToSim(float X, float Y, float Z) const
{
	// 1. strip the KITTI vehicle position (VRgrid metres, y-left)
	const double dx = static_cast<double>(X) - MapVehicleM.X;
	const double dy = static_cast<double>(Y) - MapVehicleM.Y;
	const double dz = static_cast<double>(Z) - MapVehicleM.Z;

	// 2. strip its heading, so +x is "ahead of the car" whatever the drive did
	const double c0 = FMath::Cos(-MapVehicleYawRad);
	const double s0 = FMath::Sin(-MapVehicleYawRad);
	const double lx = dx * c0 - dy * s0;
	const double ly = dx * s0 + dy * c0;

	// 3. into Unreal centimetres (y negates -- see VrgFrameReader)
	FVector Local(lx * M, -ly * M, dz * M + MapLiftM * M);

	// 4. re-apply the sim car pose
	Local = FRotator(0.0, AnchorYawDeg, 0.0).RotateVector(Local);
	return AnchorLocation + Local;
}

void AVrgSimActor::LoadMapScene()
{
	FString Dir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VrgScene="), Dir) || Dir.IsEmpty())
	{
		UE_LOG(LogTemp, Log,
		       TEXT("VRgrid sim: no -VrgScene, running the street with no map overlay"));
		return;
	}
	Dir = FPaths::ConvertRelativePathToFull(Dir);

	FString Error;
	bMapLoaded = FVrgScene::Load(Dir, MapScene, Error);
	if (!bMapLoaded)
	{
		UE_LOG(LogTemp, Error, TEXT("VRgrid sim: %s"), *Error);
		return;
	}
	MapFrame = MapScene.FrameFirst;
	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid sim: map overlay '%s' seq %s, frames %d-%d"),
	       *MapScene.SceneName, *MapScene.Sequence,
	       MapScene.FrameFirst, MapScene.FrameLast);
}

void AVrgSimActor::AppendSquareBand(TArray<FTransform>& Xf, double Inner,
                                    double Outer, double Z)
{
	// Ahead and behind span the FULL width, so they own the corners. Left and
	// right span only the inner extent, so nothing is covered twice -- double
	// translucency would draw four bright corner blocks.
	const double Thick = Outer - Inner;
	const double MidOff = (Inner + Outer) * 0.5;
	const double FullW = Outer * 2.0;
	const double InnerW = Inner * 2.0;
	const double CX[4] = { MidOff, -MidOff, 0.0, 0.0 };
	const double CY[4] = { 0.0, 0.0, MidOff, -MidOff };
	const double SX[4] = { Thick, Thick, InnerW, InnerW };
	const double SY[4] = { FullW, FullW, Thick, Thick };

	for (int32 i = 0; i < 4; ++i)
	{
		Xf.Emplace(FRotator::ZeroRotator,
		           FVector(CX[i] * M, -CY[i] * M, Z),
		           FVector(SX[i], SY[i], 0.02));
	}
}

FLinearColor AVrgSimActor::BandColourAtRange(double RangeM) const
{
	// `RangeM` arrives as a Chebyshev half-width, so this comparison is
	// exactly the band the object stands in -- the same square the ground
	// fill is drawn from.
	for (const FVrgRing& Ring : MapScene.Rings)
	{
		if (RangeM <= Ring.HalfWidthM)
		{
			const float U = FMath::Clamp(
				(Ring.CellM - MapScene.BaseCellM)
					/ FMath::Max(0.40f - MapScene.BaseCellM, 0.01f),
				0.0f, 1.0f);
			return AccuracyRamp(U);
		}
	}
	return AccuracyRamp(1.0f);
}

void AVrgSimActor::AttachScanShell(USkeletalMeshComponent* Mesh)
{
	if (Mesh == nullptr || ScanShells.Contains(Mesh))
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ScanMatPath);
	if (Base == nullptr)
	{
		return;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
	MID->SetScalarParameterValue(TEXT("PeakOpacity"), 0.0f);
	MID->SetScalarParameterValue(TEXT("RestOpacity"), 0.0f);
	// ⚑ OVERLAY, not a material swap. `SetOverlayMaterial` draws the mesh a
	//   SECOND time with this material, so the highlight is the silhouette of
	//   the actual car or person -- every curve of it -- while the object
	//   keeps its own paint underneath. Replacing slot materials would have
	//   thrown away the car's textures to get the same shape.
	Mesh->SetOverlayMaterial(MID);
	ScanShells.Add(Mesh, MID);
}

void AVrgSimActor::DriveScanShell(USkeletalMeshComponent* Mesh, double RangeM)
{
	TObjectPtr<UMaterialInstanceDynamic>* Found = ScanShells.Find(Mesh);
	if (Found == nullptr || *Found == nullptr)
	{
		return;
	}
	UMaterialInstanceDynamic* MID = *Found;

	// The material decides PER PIXEL whether the sweep edge is passing through
	// it, so the red band travels across the bodywork instead of the whole
	// object changing state at once. All this has to do is hand it where the
	// car is and where the edge is -- in CENTIMETRES, because that is what
	// world position is measured in.
	MID->SetVectorParameterValue(TEXT("EgoXY"),
		FLinearColor(static_cast<float>(AnchorLocation.X),
		             static_cast<float>(AnchorLocation.Y), 0.0f, 0.0f));
	MID->SetScalarParameterValue(TEXT("SweepR"),
		static_cast<float>(SweepHalfWidthM * 100.0));
	MID->SetScalarParameterValue(TEXT("SweepWidth"),
		FMath::Max(ScanFalloffM, 0.1f) * 100.0f);

	// Away from the edge the object rests in the colour of the band it stands
	// in; at the edge it goes to the scan colour.
	MID->SetVectorParameterValue(TEXT("RestTint"),
		BandColourAtRange(RangeM) * MapColourScale);
	MID->SetVectorParameterValue(TEXT("Tint"), ScanColour * MapColourScale);
	MID->SetScalarParameterValue(TEXT("PeakOpacity"), ScanOpacity);
	MID->SetScalarParameterValue(TEXT("RestOpacity"), ScanRestOpacity);
}

void AVrgSimActor::UpdateObjectSkins()
{
	if (MapObjects == nullptr || !bMapLoaded)
	{
		return;
	}
	if (!bSkinSimObjects || bBlindSpotOnly)
	{
		// ⚑ CLEAR THE SHELLS, not just the instances.
		//   Returning early here left every scan shell holding whatever
		//   colour it had when the mode changed, so a car caught mid-sweep
		//   stayed lit for as long as blind-spot-only was on. The MIDs
		//   persist across the toggle -- they have to, they are cached -- so
		//   turning the layer off has to turn THEM off too.
		MapObjects->ClearInstances();
		for (TPair<TObjectPtr<USkeletalMeshComponent>,
		           TObjectPtr<UMaterialInstanceDynamic>>& Pair : ScanShells)
		{
			if (Pair.Value)
			{
				Pair.Value->SetScalarParameterValue(TEXT("PeakOpacity"), 0.0f);
				Pair.Value->SetScalarParameterValue(TEXT("RestOpacity"), 0.0f);
			}
		}
		return;
	}

	// Every vehicle and person near the car wears a scan shell in its own
	// shape. The flat tile this replaces sat above the object like a slate and
	// told you nothing about what had been seen -- only where.
	MapObjects->ClearInstances();

	const double MaxR = static_cast<double>(MapDrawRadiusM);

	auto Drive = [&](USkeletalMeshComponent* Mesh)
	{
		if (Mesh == nullptr)
		{
			return;
		}
		const FVector Loc = Mesh->GetComponentLocation();
		// ⚑ CHEBYSHEV, not Euclidean. The sweep edge and the accuracy bands
		//   are squares, so an object is "at range R" when the SQUARE of
		//   half-width R touches it -- max(|dx|,|dy|). Measuring radially made
		//   a car off to one side light up well before the rectangle reached
		//   it, which is exactly the desync that shows on screen. It is also
		//   the lattice's own metric: `i_L = i_fine / k_L` is square.
		const double dxw = (Loc.X - AnchorLocation.X) / M;
		const double dyw = (Loc.Y - AnchorLocation.Y) / M;
		// Into the car's frame first, since the bands rotate with it.
		const double Yaw = FMath::DegreesToRadians(AnchorYawDeg);
		const double dx =  dxw * FMath::Cos(Yaw) + dyw * FMath::Sin(Yaw);
		const double dy = -dxw * FMath::Sin(Yaw) + dyw * FMath::Cos(Yaw);
		const double R = FMath::Max(FMath::Abs(dx), FMath::Abs(dy));
		if (R > MaxR)
		{
			// Out of range: clear the shell rather than leaving it lit.
			if (TObjectPtr<UMaterialInstanceDynamic>* Found = ScanShells.Find(Mesh))
			{
				if (*Found)
				{
					(*Found)->SetScalarParameterValue(TEXT("PeakOpacity"), 0.0f);
					(*Found)->SetScalarParameterValue(TEXT("RestOpacity"), 0.0f);
				}
			}
			return;
		}
		AttachScanShell(Mesh);
		DriveScanShell(Mesh, R);
	};

	for (const FTraffic& T : Traffic)
	{
		if (TrafficCars.IsValidIndex(T.Index))
		{
			Drive(TrafficCars[T.Index]);
		}
	}
	for (const FPed& P : Peds)
	{
		if (Pedestrians.IsValidIndex(P.Index))
		{
			Drive(Pedestrians[P.Index]);
		}
	}
}

void AVrgSimActor::UpdateSweep(float DeltaSeconds)
{
	if (MapSweep == nullptr || !bMapLoaded)
	{
		return;
	}
	if (!bShowSweepLine || bBlindSpotOnly)
	{
		MapSweep->ClearInstances();
		return;
	}

	if (MapMaxRangeM <= 0.0)
	{
		return;
	}
	const double R = SweepHalfWidthM;
	const float Phase = static_cast<float>(R / MapMaxRangeM);
	const double Half = SweepWidthM * 0.5;
	const double Inner = FMath::Max(0.05, R - Half);
	const double Outer = R + Half;

	TArray<FTransform> Xf;
	AppendSquareBand(Xf, Inner, Outer, 0.20 * M);

	if (MapSweep->GetInstanceCount() != Xf.Num())
	{
		MapSweep->ClearInstances();
		MapSweep->AddInstances(Xf, false, false);
	}
	else
	{
		for (int32 i = 0; i < Xf.Num(); ++i)
		{
			MapSweep->UpdateInstanceTransform(i, Xf[i], false, false, true);
		}
	}

	// Fades as it runs out, like a return losing strength with range -- and it
	// hides the pop when the sweep wraps back to the car.
	const float Fade = FMath::Clamp(1.0f - static_cast<float>(Phase) * 0.85f, 0.0f, 1.0f);
	const float Data[4] = {0.82f, 0.95f, 1.0f, 0.34f * Fade};
	for (int32 i = 0; i < MapSweep->GetInstanceCount(); ++i)
	{
		MapSweep->SetCustomData(i, TArrayView<const float>(Data, 4), false);
	}
	MapSweep->MarkRenderStateDirty();
}

void AVrgSimActor::BuildRingLines()
{
	if (MapRings == nullptr || bMapRingsBuilt || !bMapLoaded)
	{
		return;
	}
	bMapRingsBuilt = true;
	MapRings->ClearInstances();
	BandColours.Reset();
	if (!bShowRings)
	{
		return;
	}

	TArray<FTransform> Xf;

	// Band 0 runs all the way in to the car, and the blind spot is drawn ON
	// TOP of it. Starting the band at the blind-cone radius instead left a
	// square hole with a round disc in it, and the corners showed through as
	// black. The ring-0 band does reach the vehicle; what the cone marks is
	// that the sensor cannot SEE the middle of it.
	double Inner = 0.0;

	for (int32 r = 0; r < MapScene.Rings.Num(); ++r)
	{
		const FVrgRing& Ring = MapScene.Rings[r];
		const double Outer = Ring.HalfWidthM;
		if (Outer <= Inner)
		{
			continue;
		}

		// Colour by the cell size the band buys: 5 cm is high accuracy, 40 cm
		// is low, through the same ramp the cells used.
		const float U = FMath::Clamp(
			(Ring.CellM - MapScene.BaseCellM) / FMath::Max(0.40f - MapScene.BaseCellM, 0.01f),
			0.0f, 1.0f);
		const FLinearColor Col = AccuracyRamp(U);
		const double Z = (0.10 + 0.005 * r) * M;
		const double Thick = Outer - Inner;
		const double MidOff = (Inner + Outer) * 0.5;

		// FOUR RECTANGLES, and that is the whole band.
		//
		// A square annulus decomposes exactly: two full-width strips north and
		// south, two short strips east and west that stop at the inner edge so
		// the corners are covered once and only once. No overlap, no gaps, and
		// therefore none of the seams the round version could not avoid.
		AppendSquareBand(Xf, Inner, Outer, Z);
		for (int32 i = 0; i < 4; ++i)
		{
			BandColours.Add(Col);
		}
		Inner = Outer;
	}
	BandInstancesPerRing = 4;

	// Bands are rebuilt every frame by UpdateBands so they can follow the
	// road's elevation; nothing is placed here.

	// The blind spot fills the middle. Kept ROUND on purpose: unlike the ring
	// boundaries it is not a lattice artefact, it is h_s / tan|phi_min| --
	// a real cone of ground the sensor cannot see, and that is a circle.
	if (MapBlindCone != nullptr && bShowBlindCone)
	{
		MapBlindCone->ClearInstances();
		const double D = static_cast<double>(MapScene.BlindConeM) * 2.0;
		const FTransform Disc(FRotator::ZeroRotator,
		                      FVector(0.0, 0.0, 0.13 * M),
		                      FVector(D, D, 0.02));
		MapBlindCone->AddInstance(Disc, false);
		// A modest lift, not the full daylight gain: at 2.4x the red saturated
		// past white and the blind spot read as a pale disc rather than a
		// warning. Opacity carries the weight instead.
		const float Data[4] = {0.85f, 0.07f, 0.07f, BandOpacity + 0.34f};
		MapBlindCone->SetCustomData(0, TArrayView<const float>(Data, 4), false);
		MapBlindCone->MarkRenderStateDirty();
	}

	ApplyBandVisibility();

	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid sim: %d band rectangles over %d rings, blind spot %.2f m"),
	       Xf.Num(), MapScene.Rings.Num(), MapScene.BlindConeM);
}

double AVrgSimActor::GroundZAt(const FVector& WorldXY, double FallbackZ) const
{
	if (!bHasPath || PathPos.Num() == 0)
	{
		return FallbackZ;
	}
	// Search only the stretch near the car. The drive is 3.7 km and this runs
	// for every band slice every frame; a window keeps it to a few hundred
	// comparisons without changing the answer.
	int32 Lo = 0, Hi = PathS.Num() - 1;
	const float S0 = FMath::Fmod(DistanceM, PathLengthM);
	while (Lo + 1 < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (PathS[Mid] <= S0) { Lo = Mid; } else { Hi = Mid; }
	}
	const int32 Window = 260;
	const int32 Begin = FMath::Max(Lo - Window, 0);
	const int32 End = FMath::Min(Lo + Window, PathPos.Num() - 1);

	// ⚑ WEIGHTED, NOT NEAREST.
	//   Taking the single closest sample makes this a step function: two
	//   neighbouring lattice points can snap to different parts of the route
	//   and come back heights apart. The accuracy sheet is built from this, so
	//   at a junction -- where the route genuinely passes itself at a
	//   different level -- the sheet tore into patches. That is the scanner
	//   "glitching". An inverse-distance blend over everything within 25 m is
	//   continuous, so neighbouring cells can no longer disagree.
	double BestSq = TNumericLimits<double>::Max();
	double BestZ = FallbackZ;
	double WSum = 0.0, ZSum = 0.0;
	constexpr double BlendM = 25.0;
	const double BlendSq = BlendM * BlendM * 10000.0;
	for (int32 i = Begin; i <= End; ++i)
	{
		const double D = FVector::DistSquared2D(PathPos[i], WorldXY);
		if (D < BestSq)
		{
			BestSq = D;
			BestZ = PathPos[i].Z;
		}
		if (D < BlendSq)
		{
			const double W = 1.0 / (D + 1.0e4);   // +1 m^2, so it cannot blow up
			WSum += W;
			ZSum += W * PathPos[i].Z;
		}
	}
	if (WSum > 0.0)
	{
		return ZSum / WSum;
	}
	// Too far from any road to have a sensible height -- keep the car's.
	return (BestSq < 60.0 * 60.0 * 10000.0) ? BestZ : FallbackZ;
}

void AVrgSimActor::UpdateBands()
{
	if (BandMesh == nullptr || !bMapLoaded)
	{
		return;
	}
	// The bands moved off the instanced component; make sure nothing it used
	// to hold is still on screen.
	if (MapRings != nullptr && MapRings->GetInstanceCount() > 0)
	{
		MapRings->ClearInstances();
	}
	if (!bShowRings || bBlindSpotOnly || MapScene.Rings.Num() == 0)
	{
		BandMesh->ClearMeshSection(0);
		return;
	}

	// ⚑ ONE WATERTIGHT LATTICE, NOT A ROW OF SLABS.
	//
	//   Every band used to be an instanced box that took its height from the
	//   terrain under its own centre. Two neighbours therefore sat at slightly
	//   different heights and the road showed through the step between them --
	//   the rectangles visible inside the scan. Overlapping them to hide it
	//   only traded one artefact for another, because two translucent surfaces
	//   on top of each other double the alpha and draw a bright band instead.
	//
	//   A single grid cannot do either. Corners are SHARED, so there is
	//   nothing to step over; cells never overlap, so the alpha is uniform;
	//   and each vertex still takes its own ground height, so the whole sheet
	//   drapes over the road exactly as the slabs were trying to.
	//
	//   It is built in the VEHICLE frame. The accuracy bands are a property of
	//   the sensor relative to the car -- a square on the Chebyshev lattice,
	//   aligned to where the car is pointing NOW. Reading the route ahead is
	//   what made the scan turn before the car did.

	// Axis samples: every ring boundary is an exact stop, so no cell ever
	// straddles two accuracy bands and the colour steps cleanly.
	TArray<double> Half;
	Half.Add(0.0);
	double Prev = 0.0;
	for (const FVrgRing& Ring : MapScene.Rings)
	{
		const double B = Ring.HalfWidthM;
		if (B <= Prev) { continue; }
		const int32 Steps = FMath::Clamp(FMath::CeilToInt((B - Prev) / 12.0), 1, 8);
		for (int32 k = 1; k <= Steps; ++k)
		{
			Half.Add(Prev + (B - Prev) * k / Steps);
		}
		Prev = B;
	}
	if (Half.Num() < 2)
	{
		BandMesh->ClearMeshSection(0);
		return;
	}

	TArray<double> Axis;
	Axis.Reserve(Half.Num() * 2 - 1);
	for (int32 i = Half.Num() - 1; i >= 1; --i) { Axis.Add(-Half[i]); }
	for (double V : Half) { Axis.Add(V); }
	const int32 N = Axis.Num();

	// One world position per lattice point, shared by the four cells that meet
	// there. This is what makes the sheet continuous.
	const FRotator CarRot(0.0, AnchorYawDeg, 0.0);
	// 12 cm clears the carriageway and its markings. Raising it to 30 to chase
	// what looked like sheet-vs-road tearing at junctions was a wrong turn:
	// those pale rectangles are the resurfacing patches, which are supposed to
	// be there, and the extra height only washed the road out.
	const double Lift = 0.12 * M;
	TArray<FVector> Grid;
	Grid.SetNumUninitialized(N * N);
	for (int32 ix = 0; ix < N; ++ix)
	{
		for (int32 iy = 0; iy < N; ++iy)
		{
			const FVector P = AnchorLocation
				+ CarRot.RotateVector(FVector(Axis[ix] * M, Axis[iy] * M, 0.0));
			Grid[ix * N + iy] =
				FVector(P.X, P.Y, GroundZAt(P, AnchorLocation.Z) + Lift);
		}
	}

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<FColor> Colors;
	const int32 Cells = (N - 1) * (N - 1);
	Verts.Reserve(Cells * 4);
	Tris.Reserve(Cells * 6);

	// A cell's colour is the accuracy of the ring it sits in. Four vertices
	// per cell rather than a shared grid of them, so the band edges are hard
	// lines instead of a gradient -- the corners still coincide exactly, so
	// duplicating them costs nothing but memory.
	BandColours.Reset();
	const uint8 Alpha = static_cast<uint8>(FMath::Clamp(BandOpacity, 0.0f, 1.0f) * 255.0f);

	for (int32 ix = 0; ix < N - 1; ++ix)
	{
		for (int32 iy = 0; iy < N - 1; ++iy)
		{
			const double Cx = (Axis[ix] + Axis[ix + 1]) * 0.5;
			const double Cy = (Axis[iy] + Axis[iy + 1]) * 0.5;
			const double Cheb = FMath::Max(FMath::Abs(Cx), FMath::Abs(Cy));

			int32 RingIdx = INDEX_NONE;
			for (int32 r = 0; r < MapScene.Rings.Num(); ++r)
			{
				if (Cheb <= MapScene.Rings[r].HalfWidthM) { RingIdx = r; break; }
			}
			if (RingIdx == INDEX_NONE) { continue; }

			const FVrgRing& Ring = MapScene.Rings[RingIdx];
			const float U = FMath::Clamp(
				(Ring.CellM - MapScene.BaseCellM)
					/ FMath::Max(0.40f - MapScene.BaseCellM, 0.01f), 0.0f, 1.0f);
			FColor Col = AccuracyRamp(U).ToFColor(/*bSRGB=*/false);
			Col.A = Alpha;

			const int32 Base = Verts.Num();
			Verts.Add(Grid[ix * N + iy]);
			Verts.Add(Grid[(ix + 1) * N + iy]);
			Verts.Add(Grid[(ix + 1) * N + iy + 1]);
			Verts.Add(Grid[ix * N + iy + 1]);
			for (int32 k = 0; k < 4; ++k)
			{
				Normals.Add(FVector::UpVector);
				Tangents.Add(FProcMeshTangent(1, 0, 0));
				Colors.Add(Col);
			}
			UVs.Add(FVector2D(0, 0));
			UVs.Add(FVector2D(1, 0));
			UVs.Add(FVector2D(1, 1));
			UVs.Add(FVector2D(0, 1));

			Tris.Add(Base);     Tris.Add(Base + 2); Tris.Add(Base + 1);
			Tris.Add(Base);     Tris.Add(Base + 3); Tris.Add(Base + 2);
		}
	}

	BandMesh->CreateMeshSection(0, Verts, Tris, Normals, UVs, Colors, Tangents,
	                            /*bCreateCollision=*/false);
	BandMesh->SetVisibility(true);

	if (!bBandGeometryLogged)
	{
		bBandGeometryLogged = true;
		UE_LOG(LogTemp, Log,
		       TEXT("VRgrid sim: accuracy bands are one %dx%d lattice "
		            "(%d cells, %d verts) over %d rings, material %s"),
		       N, N, Cells, Verts.Num(), MapScene.Rings.Num(),
		       *GetNameSafe(BandMesh->GetMaterial(0)));
	}
}

void AVrgSimActor::ApplyBandVisibility()
{
	if (MapRings != nullptr)
	{
		MapRings->SetVisibility(!bBlindSpotOnly);
	}
	if (BandMesh != nullptr)
	{
		BandMesh->SetVisibility(bShowRings && !bBlindSpotOnly);
	}
}

float AVrgSimActor::ClearOfParkedCars(float AlongM) const
{
	// A crossing pedestrian has to get past the parked row to reach the
	// pavement, and stepping off between two cars is what people actually do.
	// Without this they walked straight through a parked bonnet -- the only
	// vehicle collisions left once moving traffic started yielding.
	constexpr float ClearanceM = 3.4f;     // half a car plus a shoulder
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		bool bBlocked = false;
		for (const FTraffic& T : Traffic)
		{
			if (T.SpeedMS != 0.0f)
			{
				continue;                  // only the parked row is in the way
			}
			float D = FMath::Abs(T.AlongM - AlongM);
			if (D > StreetLengthM * 0.5f) { D = StreetLengthM - D; }
			if (D < ClearanceM)
			{
				bBlocked = true;
				break;
			}
		}
		if (!bBlocked)
		{
			return AlongM;
		}
		AlongM = FMath::Fmod(AlongM + 2.5f, StreetLengthM);
	}
	return AlongM;
}

double AVrgSimActor::ZRampAt(float AlongM) const
{
	// ⚑ BREAK THE TIE BETWEEN TWO PASSES OVER THE SAME GROUND.
	//   This route crosses itself, and at a junction the two legs sit as
	//   little as 3 cm apart in height -- two carriageways effectively
	//   coplanar. The depth buffer cannot separate them, so the junction
	//   renders as a patchwork of both surfaces flickering through each other,
	//   which is what reads as "scattered".
	//   Two centimetres spread over 3.7 km of route is a gradient no one can
	//   see, and it guarantees that any two points far apart ALONG the drive
	//   are also apart in depth. Everything laid on the road -- markings,
	//   patches -- takes the same ramp, so nothing shifts relative to its own
	//   stretch.
	if (PathLengthM <= 0.0f)
	{
		return 0.0;
	}
	const float S = FMath::Fmod(FMath::Max(AlongM, 0.0f), PathLengthM);
	return (S / PathLengthM) * 0.02 * M;
}

float AVrgSimActor::ScriptedLeadM() const
{
	// ⚑ LEAD BY THE TIME TO CLEAR, NOT THE TIME TO ARRIVE.
	//   This used to lead by (PaveY + LaneY) / CrossSpeed -- the time to REACH
	//   the ego's lane -- which by construction puts a pedestrian in the lane
	//   at the exact moment the car gets there. That only works if the car
	//   then stops, and stopping is what we are removing. Leading by the time
	//   to get past the FAR edge of the lane means they are clear as it
	//   arrives, which is what the recorded crossing actually looks like.
	const float PaveY = RoadWidthM * 0.5f + 2.8f;

	// Time for them to reach the NEAR edge of the ego's lane -- the moment the
	// car can first see them as something to yield to.
	const float ToLaneY = PaveY + EgoLaneM - LaneHalfWidthM;
	const float TimeToLaneS = ToLaneY / FMath::Max(CrossSpeedMS, 0.1f);

	// ⚑ PLUS ROOM TO BRAKE, or the yield cannot happen.
	//   Leading by the crossing time alone put the car right on top of them
	//   the instant they stepped into the lane -- no distance to shed speed
	//   in, so it either ploughed on at cruise or slammed. Twenty metres is
	//   more than the 6.2 m it takes to go 8.2 -> 3.4 m/s at 4.5 m/s2, so the
	//   car has already settled to the yield speed when it arrives, which is
	//   what the recorded crossing looks like.
	//
	//   It is also self-correcting in the safe direction: the car slows on
	//   approach, so it takes LONGER than this estimate, and the crosser gets
	//   more room than the arithmetic promises, never less.
	const float BrakingRoomM = 20.0f;
	return FMath::Max(EgoSpeedMS, 4.0f) * TimeToLaneS + BrakingRoomM;
}

void AVrgSimActor::TriggerScriptedCross()
{
	if (!Peds.IsValidIndex(ScriptedPedIndex))
	{
		return;
	}
	FPed& P = Peds[ScriptedPedIndex];

	// Step off the FAR kerb, so the car (and the camera) watches them cross
	// the whole road rather than appearing at the near edge.
	const float PaveY = RoadWidthM * 0.5f + 2.8f;
	const float StartY = -PaveY;

	const float CrossAt = FMath::Fmod(FMath::Max(ScriptedCrossAtM, 0.0f),
	                                  StreetLengthM);
	P.AlongM = ClearOfParkedCars(CrossAt);
	P.LateralM = StartY;
	P.CrossTarget = -StartY;
	P.DwellS = 0.0f;
	P.SpeedMS = 0.25f;          // barely walks along; this crossing is lateral

	float ToGo = CrossAt - DistanceM;
	if (ToGo < 0.0f) { ToGo += StreetLengthM; }
	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid sim: crossing at %.0fm (the one in the data), car %.0fm "
	            "short, clears the lane in %.1fs"),
	       P.AlongM, ToGo,
	       (PaveY + EgoLaneM + LaneHalfWidthM) / FMath::Max(CrossSpeedMS, 0.1f));
}

void AVrgSimActor::ToggleBands()
{
	bBlindSpotOnly = !bBlindSpotOnly;
	ApplyBandVisibility();
	UE_LOG(LogTemp, Log, TEXT("VRgrid sim: %s"),
	       bBlindSpotOnly ? TEXT("blind spot only") : TEXT("all accuracy bands"));
}

void AVrgSimActor::BindSimInput()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}
	EnableInput(PC);
	if (InputComponent != nullptr)
	{
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AVrgSimActor::ToggleBands);
		InputComponent->BindKey(EKeys::N, IE_Pressed, this,
		                        &AVrgSimActor::TriggerScriptedCross);
	}
}

void AVrgSimActor::RebuildMapCells(UInstancedStaticMeshComponent* Ism,
                                   const TArray<FVrgCell>& Cells)
{
	if (Ism == nullptr) { return; }
	Ism->ClearInstances();
	if (Cells.Num() == 0) { return; }

	const double R2 = static_cast<double>(MapDrawRadiusM) * MapDrawRadiusM;
	TArray<FTransform> Xf;
	TArray<int32> Kept;
	TArray<FLinearColor> Cols;
	Xf.Reserve(Cells.Num());
	Kept.Reserve(Cells.Num());
	Cols.Reserve(Cells.Num());

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FVrgCell& Cell = Cells[i];
		// Cull in the vehicle frame, before the transform.
		const double dx = static_cast<double>(Cell.X) - MapVehicleM.X;
		const double dy = static_cast<double>(Cell.Y) - MapVehicleM.Y;
		if (dx * dx + dy * dy > R2)
		{
			continue;
		}

		// THIS is what makes the band climb over what it has seen: the cell
		// carries its own height, so the surface follows a kerb, a parked car
		// or a facade instead of lying flat on the road. A 2.5D map is exactly
		// this -- one elevation per cell -- and drawing it at z is the whole
		// difference between a coloured floor and a measured surface.
		const double Edge = static_cast<double>(Cell.CellM) * CellFill;
		Xf.Emplace(FRotator(0.0, AnchorYawDeg, 0.0),
		           MapToSim(Cell.X, Cell.Y, Cell.Z),
		           FVector(Edge, Edge, CellThicknessM));
		Kept.Add(i);

		if (bCellsUseBandColour)
		{
			// Which accuracy band measured this cell? The cell size says so
			// directly -- it IS the ring's resolution -- so match on that
			// rather than re-deriving a range the window may have shifted.
			float U = 0.0f;
			float Best = TNumericLimits<float>::Max();
			for (const FVrgRing& Ring : MapScene.Rings)
			{
				const float D = FMath::Abs(Ring.CellM - Cell.CellM);
				if (D < Best)
				{
					Best = D;
					U = FMath::Clamp(
						(Ring.CellM - MapScene.BaseCellM)
							/ FMath::Max(0.40f - MapScene.BaseCellM, 0.01f),
						0.0f, 1.0f);
				}
			}
			Cols.Add(AccuracyRamp(U));
		}
		else
		{
			Cols.Add(FLinearColor(Cell.R / 255.0f, Cell.G / 255.0f, Cell.B / 255.0f));
		}
	}
	if (Xf.Num() == 0) { return; }
	Ism->AddInstances(Xf, false, true);

	float Data[4];
	for (int32 i = 0; i < Kept.Num(); ++i)
	{
		Data[0] = Cols[i].R * MapColourScale;
		Data[1] = Cols[i].G * MapColourScale;
		Data[2] = Cols[i].B * MapColourScale;
		Data[3] = CellOpacity;
		Ism->SetCustomData(i, TArrayView<const float>(Data, 4), false);
	}
	Ism->MarkRenderStateDirty();
}

void AVrgSimActor::RebuildMapPoints(UInstancedStaticMeshComponent* Ism,
                                    const TArray<FVrgPoint>& Points,
                                    float SizeM, int32 Stride)
{
	if (Ism == nullptr) { return; }
	Ism->ClearInstances();
	if (Points.Num() == 0) { return; }

	const int32 Step = FMath::Max(1, Stride);
	const double Edge = static_cast<double>(SizeM);
	const double R2 = static_cast<double>(MapDrawRadiusM) * MapDrawRadiusM;
	TArray<FTransform> Xf;
	TArray<int32> Kept;
	Xf.Reserve(Points.Num() / Step + 1);
	Kept.Reserve(Points.Num() / Step + 1);
	for (int32 i = 0; i < Points.Num(); i += Step)
	{
		const double dx = static_cast<double>(Points[i].X) - MapVehicleM.X;
		const double dy = static_cast<double>(Points[i].Y) - MapVehicleM.Y;
		if (dx * dx + dy * dy > R2)
		{
			continue;
		}
		// Flattened: the cylinder is a disc seen from the car, not a puck.
		Xf.Emplace(FRotator::ZeroRotator,
		           MapToSim(Points[i].X, Points[i].Y, Points[i].Z),
		           FVector(Edge, Edge, Edge * 0.45));
		Kept.Add(i);
	}
	if (Xf.Num() == 0) { return; }
	Ism->AddInstances(Xf, false, true);

	float Data[4];
	for (int32 i = 0; i < Kept.Num(); ++i)
	{
		const FVrgPoint& Pt = Points[Kept[i]];
		FVrgFrameReader::ColorToCustomData(Pt.R, Pt.G, Pt.B, Pt.A, Data);
		for (int32 k = 0; k < 3; ++k) { Data[k] *= MapColourScale; }
		Ism->SetCustomData(i, TArrayView<const float>(Data, 4), false);
	}
	Ism->MarkRenderStateDirty();
}

void AVrgSimActor::ApplyMapFrame(const FVrgFrame& Frame)
{
	// The reader already converted the header pose into Unreal units; undo it
	// to recover the KITTI vehicle pose in VRgrid metres, which is what the
	// chunk payloads are in.
	MapVehicleM = FVector(Frame.VehicleLocation.X / M,
	                      -Frame.VehicleLocation.Y / M,
	                      Frame.VehicleLocation.Z / M);
	MapVehicleYawRad = FMath::DegreesToRadians(-Frame.VehicleYawDeg);

	// Present-but-empty clears; absent leaves what is on screen. The map
	// layers are only written every MapInterval frames.
	if (Frame.bHasPoints && bShowSweep)
	{
		RebuildMapPoints(MapPoints, Frame.Points, PointSizeM, PointStride);
	}
	else if (MapPoints != nullptr)
	{
		MapPoints->ClearInstances();
	}
	if (MapGhosts != nullptr) { MapGhosts->ClearInstances(); }
	if (Frame.bHasOccupied && bShowCells)
	{
		RebuildMapCells(MapOccupied, Frame.Occupied);
	}
	else if (MapOccupied != nullptr)
	{
		MapOccupied->ClearInstances();
	}
	if (bShowFreeUnknown)
	{
		if (Frame.bHasFree)    { RebuildMapCells(MapFree, Frame.Free); }
		if (Frame.bHasUnknown) { RebuildMapCells(MapUnknown, Frame.Unknown); }
	}
}

void AVrgSimActor::AdvanceMap(float DeltaSeconds)
{
	if (!bMapLoaded || !bShowMap || MapScene.FrameCount <= 0)
	{
		return;
	}

	// Ring boundaries ride the car, exactly as they ride the vehicle transform
	// in Rerun.
	if (MapBlindCone != nullptr)
	{
		MapBlindCone->SetWorldLocation(AnchorLocation);
	}

	const float Step = 1.0f / FMath::Max(MapScene.PlaybackFps, 1.0f);
	MapAccumulator += DeltaSeconds;
	if (MapAccumulator < Step)
	{
		return;
	}
	MapAccumulator = 0.0f;

	int32 Next = MapFrame + 1;
	if (Next > MapScene.FrameLast)
	{
		Next = MapScene.FrameFirst;
	}
	MapFrame = Next;

	FVrgFrame Frame;
	FString Error;
	if (FVrgFrameReader::LoadFrame(MapScene.FramePath(MapFrame), Frame, Error))
	{
		ApplyMapFrame(Frame);
		if (Frame.bHasOccupied && MapOccupied != nullptr && (MapFrame % 20) == 0)
		{
			FTransform First;
			const bool bGot = MapOccupied->GetInstanceCount() > 0
				&& MapOccupied->GetInstanceTransform(0, First, true);
			UE_LOG(LogTemp, Log,
			       TEXT("VRgrid sim: frame %d -- %d of %d cells drawn within %.0f m "
			            "| cells vis=%d mat=%s | rings n=%d vis=%d | first=(%.0f %.0f %.0f) "
			            "scale=(%.2f %.2f %.2f)"),
			       MapFrame, MapOccupied->GetInstanceCount(), Frame.Occupied.Num(),
			       MapDrawRadiusM,
			       MapOccupied->IsVisible() ? 1 : 0,
			       MapOccupied->GetMaterial(0) ? *MapOccupied->GetMaterial(0)->GetName()
			                                   : TEXT("NULL"),
			       MapRings ? MapRings->GetInstanceCount() : -1,
			       (MapRings && MapRings->IsVisible()) ? 1 : 0,
			       bGot ? First.GetLocation().X : 0.0, bGot ? First.GetLocation().Y : 0.0,
			       bGot ? First.GetLocation().Z : 0.0,
			       bGot ? First.GetScale3D().X : 0.0, bGot ? First.GetScale3D().Y : 0.0,
			       bGot ? First.GetScale3D().Z : 0.0);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VRgrid sim: %s"), *Error);
	}
}

void AVrgSimActor::BeginPlay()
{
	Super::BeginPlay();

	FString ShotSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("VrgShot="), ShotSpec))
	{
		ShotAtFrame = FCString::Atoi(*ShotSpec);
	}
	bDiagnostics = FParse::Param(FCommandLine::Get(), TEXT("VrgDiag"));
	bOnlyEgoShadow = FParse::Param(FCommandLine::Get(), TEXT("VrgOnlyEgoShadow"));

	// Read the shadow cvars BACK. Setting one in the wrong ini section fails
	// silently, and a silent no-op is indistinguishable from "that was not the
	// cause" -- which cost a long time chasing a detached shadow.
	{
		auto Show = [](const TCHAR* Name)
		{
			IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
			UE_LOG(LogTemp, Warning, TEXT("VRgrid cvar: %s = %s"), Name,
			       V ? *V->GetString() : TEXT("<does not exist>"));
		};
		Show(TEXT("r.Shadow.Virtual.Enable"));
		Show(TEXT("r.Shadow.Virtual.NormalBias"));
		Show(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"));
		Show(TEXT("r.Shadow.Virtual.Cache"));
	}

	// `-VrgSpeed=<m/s>` overrides cruise. At 0 the car is stationary, which is
	// how a shadow that LAGS is told apart from one that is merely offset:
	// lag vanishes when the caster stops, a projection error does not.
	FString SpeedSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("VrgSpeed="), SpeedSpec))
	{
		SpeedMS = FCString::Atof(*SpeedSpec);
		UE_LOG(LogTemp, Warning, TEXT("VRgrid sim: cruise overridden to %.2f m/s"), SpeedMS);
	}

	// `-VrgCrossAt=<metres>` moves the scheduled crossing. The real one is at
	// 3581 m, which is 7.5 minutes into a lap -- too far to sit through when
	// what is being checked is the trigger, the lead distance and the yield,
	// none of which care where on the route they happen.
	FString CrossSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("VrgCrossAt="), CrossSpec))
	{
		ScriptedCrossAtM = FCString::Atof(*CrossSpec);
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid sim: crossing MOVED to %.0f m for this run "
		            "(the one in the data is at 3581 m)"), ScriptedCrossAtM);
	}

	// Order matters: the route is read out of the export, and every piece of
	// the world is then laid along it.
	LoadMapScene();
	LoadPath();
	BuildLighting();
	BuildStreet();
	BuildBuildings();
	BuildStreetFurniture();
	SpawnTraffic();
	SpawnPedestrians();
	BuildRingLines();
	BindSimInput();
	Surfaces->MarkRenderStateDirty();
	Emissives->MarkRenderStateDirty();

	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, CarMeshPath))
	{
		Car->SetSkeletalMesh(Mesh);
	}
	else
	{
		// Loud: without the plugin content the ego vehicle is invisible and the
		// chase camera looks like it is following nothing.
		UE_LOG(LogTemp, Error,
		       TEXT("VRgrid sim: could not load %s -- is the "
		            "ChaosModularVehicleExamples plugin enabled?"), CarMeshPath);
	}

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		PC->SetViewTarget(this);
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}

	// ⚑ A SHADOW THAT LAGS IS A CACHING PROBLEM, NOT A BIAS ONE.
	//   Virtual shadow maps cache their pages and only redraw the ones an
	//   object invalidates when it moves. A component that is not MOVABLE
	//   never issues that invalidation, so its shadow stays where the object
	//   was when the page was last drawn -- at 8.2 m/s that is metres behind,
	//   which reads exactly like the car floating above a detached shadow.
	//   Nothing here ever set mobility, so it was whatever the default gave,
	//   and the ego car (a constructor subobject) and the traffic (NewObject
	//   at runtime) do not have to get the same default.
	{
		const int32 EgoMob = static_cast<int32>(Car->Mobility.GetValue());
		const int32 CarMob = TrafficCars.Num() > 0
			? static_cast<int32>(TrafficCars[0]->Mobility.GetValue()) : -1;
		const int32 PedMob = Pedestrians.Num() > 0
			? static_cast<int32>(Pedestrians[0]->Mobility.GetValue()) : -1;
		UE_LOG(LogTemp, Log,
		       TEXT("VRgrid shadow: mobility before fix -- ego=%d traffic=%d ped=%d "
		            "(0=Static 1=Stationary 2=Movable)"), EgoMob, CarMob, PedMob);

		Car->SetMobility(EComponentMobility::Movable);
		for (USkeletalMeshComponent* C : TrafficCars)
		{
			if (C) { C->SetMobility(EComponentMobility::Movable); }
		}
		for (USkeletalMeshComponent* P : Pedestrians)
		{
			if (P) { P->SetMobility(EComponentMobility::Movable); }
		}
	}

	// `-VrgOnlyEgoShadow`: silence every other caster so whatever shadow is
	// left in frame is unambiguously the ego car's. Settles "is the car's own
	// shadow detached, or is that some other object's shadow near it" without
	// any interpretation of the picture.
	if (bOnlyEgoShadow)
	{
		Surfaces->SetCastShadow(false);
		Emissives->SetCastShadow(false);
		RoadMesh->SetCastShadow(false);
		for (USkeletalMeshComponent* C : TrafficCars) { if (C) { C->SetCastShadow(false); } }
		for (USkeletalMeshComponent* P : Pedestrians) { if (P) { P->SetCastShadow(false); } }
		Car->SetCastShadow(true);
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid sim: ONLY THE EGO CAR CASTS A SHADOW this run"));
	}
	// The mirror image: everything EXCEPT the ego. If the traffic cars -- same
	// mesh, same motion, but built with NewObject rather than as constructor
	// subobjects -- have shadows that sit under them, the fault is specific to
	// the ego car. If theirs trail too, it is every moving mesh in the scene.
	if (FParse::Param(FCommandLine::Get(), TEXT("VrgNoEgoShadow")))
	{
		Surfaces->SetCastShadow(false);
		Emissives->SetCastShadow(false);
		RoadMesh->SetCastShadow(false);
		for (USkeletalMeshComponent* P : Pedestrians) { if (P) { P->SetCastShadow(false); } }
		for (USkeletalMeshComponent* C : TrafficCars) { if (C) { C->SetCastShadow(true); } }
		Car->SetCastShadow(false);
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid sim: ONLY THE TRAFFIC CARS CAST SHADOWS this run"));
	}

	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid sim: %d surfaces, %d emissives, %d lamps, %d cars, %d people"),
	       Surfaces->GetInstanceCount(), Emissives->GetInstanceCount(),
	       StreetLamps.Num(), TrafficCars.Num(), Pedestrians.Num());
}

void AVrgSimActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (Camera != nullptr)
	{
		Camera->GetCameraView(DeltaTime, OutResult);
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
}

void AVrgSimActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;

	// --- ego: follow, do not drive through -------------------------------
	//
	// The first version advanced the ego at a constant speed and passed
	// straight through whatever was in the lane, which is the one thing a
	// driving scenario cannot do in front of a panel about obstacle
	// perception. It now runs a plain distance-keeper: close the gap at
	// cruise, match the lead car inside FollowGapM, brake hard under half of
	// it. No physics -- just a speed that respects what is in front.
	const float LaneY = EgoLaneM;
	float Gap = 0.0f;
	const int32 Lead = CarAhead(DistanceM, LaneY, Gap);

	float TargetSpeed = SpeedMS;
	if (Lead >= 0)
	{
		const float LeadSpeed = FMath::Abs(Traffic[Lead].SpeedMS);
		if (Gap < FollowGapM * 0.5f)
		{
			TargetSpeed = FMath::Min(SpeedMS, LeadSpeed * 0.55f);
		}
		else if (Gap < FollowGapM)
		{
			// Ease between "match the lead" and "cruise" across the band, so
			// the car settles behind traffic instead of surging and braking.
			const float T = (Gap - FollowGapM * 0.5f) / (FollowGapM * 0.5f);
			TargetSpeed = FMath::Lerp(LeadSpeed * 0.55f, SpeedMS, T);
		}
	}
	// A pedestrian in the EGO'S LANE ahead slows the car.
	//
	// ⚑ Three things were wrong here and together they stalled the drive for
	//   ~10 s at a time (measured: 22 m covered in 9 s, target pinned at 2.0):
	//
	//   1. The test was "anywhere in the 14 m carriageway", so somebody on the
	//      FAR side of the road, in the oncoming lane, braked the car.
	//   2. It clamped to a hard 2.0 m/s. A pedestrian walking the same way at
	//      1.5 m/s is then closed on at 0.5 m/s, so the gate holds for tens of
	//      seconds -- the car cannot clear what it is waiting for.
	//   3. Crossers never left the road (see the dwell below).
	//
	//   Now: only the ego's own lane, and the limit eases with the gap so the
	//   car always closes and clears instead of pacing them.
	bool bPedGate = false;
	for (const FPed& P : Peds)
	{
		if (FMath::Abs(P.LateralM - LaneY) > LaneHalfWidthM)
		{
			continue;                       // not in this lane
		}
		float PedGap = P.AlongM - DistanceM;
		if (PedGap < 0.0f) { PedGap += StreetLengthM; }
		if (PedGap <= 0.0f || PedGap > 16.0f)
		{
			continue;
		}
		// ⚑ SLOW TO 3.4, DO NOT STOP.
		//   The recorded drive yields exactly once (frames 4377-4412) and the
		//   ego goes 7.88 -> 3.44 m/s without halting. In 454 s it comes to a
		//   stop one time, for 2.6 s, on an empty road -- not for a person.
		//   Stopping dead for every pedestrian was authored here.
		//
		//   The old zero floor was not arbitrary: a 3.2 m/s floor once let the
		//   car reach somebody still mid-road. The answer is to give the
		//   crosser time to CLEAR (see TriggerScriptedCross, which now leads by
		//   the time to cross the whole road) and keep a panic gap as a net.
		float Limit = YieldFloorMS;
		if (PedGap > YieldStopGapM)
		{
			const float Ease = FMath::Clamp(
				(PedGap - YieldStopGapM) / FMath::Max(16.0f - YieldStopGapM, 0.1f),
				0.0f, 1.0f);
			Limit = FMath::Lerp(YieldFloorMS, SpeedMS, Ease);
		}
		if (PedGap <= YieldPanicGapM)
		{
			// Never fires when the timing is right. Better than a collision.
			Limit = 0.0f;
			UE_CLOG(bDiagnostics, LogTemp, Warning,
			        TEXT("VRgrid sim: PANIC STOP -- ped %.1f m ahead in lane"), PedGap);
		}
		TargetSpeed = FMath::Min(TargetSpeed, Limit);
		bPedGate = true;
	}

	// Diagnostic: what is actually holding the speed down, and on which lap.
	if (bDiagnostics)
	{
		static float NextLog = 0.0f;
		if (Elapsed >= NextLog)
		{
			NextLog = Elapsed + 3.0f;
			const float LeadSpeed = (Lead >= 0) ? FMath::Abs(Traffic[Lead].SpeedMS) : -1.0f;
			UE_LOG(LogTemp, Log,
			       TEXT("VRgrid ego: t=%.0fs lap=%d along=%.0fm speed=%.1f target=%.1f "
			            "cruise=%.1f | lead=%d gap=%.1f leadspeed=%.1f | pedgate=%d"),
			       Elapsed, FMath::FloorToInt(Elapsed * SpeedMS / StreetLengthM),
			       DistanceM, EgoSpeedMS, TargetSpeed, SpeedMS,
			       Lead, (Lead >= 0) ? Gap : -1.0f, LeadSpeed,
			       bPedGate ? 1 : 0);
		}
	}

	// Brake harder than it accelerates. At a single gentle rate the car took
	// about a second and a half to shed 14 m/s, which is twenty metres of road
	// -- longer than the gap it was braking for.
	const float Rate = (TargetSpeed < EgoSpeedMS) ? 4.5f : 1.8f;
	EgoSpeedMS = FMath::FInterpTo(EgoSpeedMS, TargetSpeed, DeltaSeconds, Rate);
	DistanceM = FMath::Fmod(DistanceM + EgoSpeedMS * DeltaSeconds, StreetLengthM);

	// Pose straight off the route, so the car turns where the drive turned
	// and climbs where it climbed. The weave that used to stand in for
	// steering is gone -- there is real steering now, and adding a wobble on
	// top of a 5,158-degree route only fought it.
	FVector CarPos; float Yaw;
	PathAt(DistanceM, LaneY, CarPos, Yaw);

	Car->SetWorldLocation(CarPos);
	Car->SetWorldRotation(FRotator(0.0f, Yaw, 0.0f));

	// ⚑ IS ANYTHING ACTUALLY FLOATING?
	//   A shadow 8 m from its caster at a 68 degree sun means the caster is
	//   ~20 m above the surface it lands on. That is not a shadow bug, it is
	//   an object in the wrong place -- so measure every object's height over
	//   the ground rather than argue about it from screenshots.
	if (bDiagnostics)
	{
		static bool bFloatChecked = false;
		if (!bFloatChecked && Elapsed > 3.0f)
		{
			bFloatChecked = true;
			auto Report = [&](const TCHAR* What,
			                  const TArray<TObjectPtr<USkeletalMeshComponent>>& Set)
			{
				double Worst = 0.0; int32 WorstIdx = INDEX_NONE; int32 Floating = 0;
				for (int32 i = 0; i < Set.Num(); ++i)
				{
					if (!Set[i]) { continue; }
					const FVector L = Set[i]->GetComponentLocation();
					const double Over = (L.Z - GroundZAt(L, L.Z)) / M;
					if (Over > 1.0) { ++Floating; }
					if (FMath::Abs(Over) > FMath::Abs(Worst)) { Worst = Over; WorstIdx = i; }
				}
				UE_LOG(LogTemp, Warning,
				       TEXT("VRgrid float: %s -- %d of %d more than 1 m over the "
				            "ground, worst #%d at %.2f m"),
				       What, Floating, Set.Num(), WorstIdx, Worst);
			};
			Report(TEXT("traffic"), TrafficCars);
			Report(TEXT("people "), Pedestrians);
			const FVector E = Car->GetComponentLocation();
			UE_LOG(LogTemp, Warning, TEXT("VRgrid float: ego     -- %.2f m over the ground"),
			       (E.Z - GroundZAt(E, E.Z)) / M);
		}
	}


	for (int32 i = 0; i < 2; ++i)
	{
		USpotLightComponent* S = (i == 0) ? HeadlightL : HeadlightR;
		const float Offset = (i == 0) ? 0.72f : -0.72f;
		const FVector Fwd = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(1, 0, 0));
		const FVector Rgt = FRotator(0.0, Yaw, 0.0).RotateVector(FVector(0, 1, 0));
		S->SetWorldLocation(CarPos + Fwd * (2.0 * M) + Rgt * (Offset * M)
		                    + FVector(0, 0, 0.62 * M));
		S->SetWorldRotation(FRotator(-3.0f, Yaw, 0.0f));
		S->SetVisibility(!bDaytime);
	}

	// --- traffic: keep its own gaps --------------------------------------
	for (FTraffic& T : Traffic)
	{
		if (!TrafficCars.IsValidIndex(T.Index) || TrafficCars[T.Index] == nullptr)
		{
			continue;
		}
		if (T.SpeedMS != 0.0f)
		{
			float OwnGap = 0.0f;
			const int32 Front = CarAhead(T.AlongM, T.LateralM, OwnGap);
			// Moving cars in the same lane stack up rather than interpenetrate.
			float Step = T.SpeedMS;
			if (Front >= 0 && Front != T.Index && OwnGap < 9.0f)
			{
				Step *= FMath::Clamp(OwnGap / 9.0f, 0.0f, 1.0f);
			}

			// ⚑ AND THEY YIELD TO PEOPLE. Only the ego checked pedestrians,
			//   so the moment a crossing landed in front of other traffic the
			//   car drove straight through them -- in full view, beside an ego
			//   that was carefully stopping for the same person.
			for (const FPed& P : Peds)
			{
				if (FMath::Abs(P.LateralM - T.LateralM) > LaneHalfWidthM)
				{
					continue;
				}
				// "Ahead" depends on which way this one is pointing.
				float PedGap = (T.SpeedMS >= 0.0f) ? (P.AlongM - T.AlongM)
				                                   : (T.AlongM - P.AlongM);
				if (PedGap < 0.0f) { PedGap += StreetLengthM; }
				if (PedGap <= 0.0f || PedGap > 16.0f)
				{
					continue;
				}
				const float Scale = (PedGap <= YieldStopGapM)
					? 0.0f
					: FMath::Clamp((PedGap - YieldStopGapM)
					               / FMath::Max(16.0f - YieldStopGapM, 0.1f), 0.0f, 1.0f);
				Step *= Scale;
			}
			T.AlongM += Step * DeltaSeconds;
			if (T.AlongM > StreetLengthM) { T.AlongM -= StreetLengthM; }
			if (T.AlongM < 0.0f) { T.AlongM += StreetLengthM; }
		}
		FVector TPos; float TYaw;
		PathAt(T.AlongM, T.LateralM, TPos, TYaw);
		TrafficCars[T.Index]->SetWorldLocation(TPos);
		TrafficCars[T.Index]->SetWorldRotation(
			FRotator(0.0f, TYaw + (T.SpeedMS < 0.0f ? 180.0f : 0.0f), 0.0f));
	}

	// ⚑ ONE CROSSING PER LAP, WHERE THE REAL ONE HAPPENED.
	//   Not a timer. `ScriptedCrossAtM` is 3581 m because that is where seq 00
	//   has its single clean lateral crossing. The trigger fires when the car
	//   comes within the lead distance of that point, so the pedestrian steps
	//   off in time to be mid-road as the car arrives -- and the wrap is
	//   handled so it re-arms once per lap rather than re-firing.
	if (bScriptedCross && Peds.IsValidIndex(ScriptedPedIndex))
	{
		const float CrossAt = FMath::Fmod(FMath::Max(ScriptedCrossAtM, 0.0f),
		                                  StreetLengthM);
		float ToGo = CrossAt - DistanceM;
		if (ToGo < 0.0f) { ToGo += StreetLengthM; }
		if (!bScriptedArmed && ToGo > StreetLengthM * 0.5f)
		{
			bScriptedArmed = true;          // past it; re-arm for the next lap
		}
		if (bScriptedArmed && ToGo <= ScriptedLeadM())
		{
			bScriptedArmed = false;
			TriggerScriptedCross();
		}
	}

	// TEMPORARY VERIFICATION: did anything actually drive through a person?
	// Same lane, and overlapping along the road, is a hit however it happened.
	{
		auto Overlap = [&](float AlongA, float LatA, float AlongB, float LatB) -> bool
		{
			if (FMath::Abs(LatA - LatB) > 1.3f) { return false; }
			float D = FMath::Abs(AlongA - AlongB);
			if (D > StreetLengthM * 0.5f) { D = StreetLengthM - D; }
			return D < 2.2f;
		};
		// How close does the ego actually get to anyone? "Driving through"
		// someone and "shaving past them at 50 km/h" look identical from the
		// chase camera, and only one of them is a collision.
		for (const FPed& P : Peds)
		{
			float DAlong = FMath::Abs(P.AlongM - DistanceM);
			if (DAlong > StreetLengthM * 0.5f) { DAlong = StreetLengthM - DAlong; }
			const float DLat = FMath::Abs(P.LateralM - LaneY);
			if (DAlong < 3.0f && DLat < 2.5f)
			{
				static float NextNear = 0.0f;
				if (Elapsed >= NextNear)
				{
					NextNear = Elapsed + 0.5f;
					UE_LOG(LogTemp, Warning,
					       TEXT("VRgrid NEAR: t=%.0fs ped #%d gap along %.2f m, "
					            "lateral %.2f m, ego %.1f m/s (car half-width 0.95, "
					            "person 0.3)"),
					       Elapsed, P.Index, DAlong, DLat, EgoSpeedMS);
				}
			}
			if (Overlap(DistanceM, LaneY, P.AlongM, P.LateralM))
			{
				UE_LOG(LogTemp, Warning,
				       TEXT("VRgrid HIT: ego through ped at %.0fm (speed %.1f)"),
				       P.AlongM, EgoSpeedMS);
			}
			for (const FTraffic& T : Traffic)
			{
				if (Overlap(T.AlongM, T.LateralM, P.AlongM, P.LateralM))
				{
					static float NextHitLog = 0.0f;
					if (Elapsed >= NextHitLog)
					{
						NextHitLog = Elapsed + 1.0f;
						UE_LOG(LogTemp, Warning,
						       TEXT("VRgrid HIT: traffic #%d (y=%.1f v=%.1f) through "
						            "ped #%d (y=%.1f cross=%.1f) at %.0fm"),
						       T.Index, T.LateralM, T.SpeedMS,
						       P.Index, P.LateralM, P.CrossTarget, P.AlongM);
					}
				}
			}
		}
	}

	// --- people ------------------------------------------------------------
	for (FPed& P : Peds)
	{
		// ⚑ Walk STRAIGHT across, not diagonally.
		//   While crossing, the along-position is frozen. It used to keep
		//   advancing, so a pedestrian drifted up to twenty metres down the
		//   road during the fourteen seconds a crossing takes -- and the gap
		//   in the parked row that was picked when they stepped off was not
		//   where they arrived. They walked out through a parked bonnet.
		//   People cross perpendicular to the kerb anyway.
		if (P.CrossTarget == 0.0f)
		{
			P.AlongM += P.SpeedMS * DeltaSeconds;
			if (P.AlongM > StreetLengthM) { P.AlongM -= StreetLengthM; }
			if (P.AlongM < 0.0f) { P.AlongM += StreetLengthM; }
		}

		if (P.CrossTarget != 0.0f)
		{
			// Crossing: step across at a walking pace.
			P.LateralM = FMath::FInterpConstantTo(P.LateralM, P.CrossTarget,
			                                      DeltaSeconds, CrossSpeedMS);
			if (FMath::Abs(P.LateralM - P.CrossTarget) < 0.05f)
			{
				// Arrived. STAY on this pavement for a while -- flipping the
				// target here is what had them ping-ponging across the road
				// forever, so the carriageway was never clear.
				const bool bScripted = (&P - Peds.GetData() == ScriptedPedIndex);
				P.CrossTarget = 0.0f;
				// The scheduled crosser waits for its next cue instead of
				// picking up an ambient dwell -- and gets its walking pace
				// back, having been slowed to a crawl for the crossing.
				P.DwellS = bScripted ? 0.0f : FMath::FRandRange(8.0f, 26.0f);
				if (bScripted)
				{
					P.SpeedMS = (P.SpeedMS >= 0.0f) ? 1.2f : -1.2f;
				}
				UE_CLOG(bDiagnostics, LogTemp, Log,
				        TEXT("VRgrid ped: #%d reached the far pavement at %.0fm, "
				             "waits %.0fs"), P.Index, P.AlongM, P.DwellS);
			}
		}
		else if (P.DwellS > 0.0f)
		{
			P.DwellS -= DeltaSeconds;
			if (P.DwellS <= 0.0f)
			{
				// ⚑ Only the scheduled crosser enters the carriageway.
				//   Twenty-six ambient pedestrians re-crossing on 8-26 s
				//   dwells meant the car met somebody in its lane almost
				//   continuously. The recorded drive has ONE crossing.
				if (!bAmbientPedsCross && P.Index != ScriptedPedIndex)
				{
					P.DwellS = 12.0f;            // stay put, look again later
					continue;
				}
				// Step off between parked cars, not through one.
				P.AlongM = ClearOfParkedCars(P.AlongM);
				P.CrossTarget = -P.LateralM;     // cross back
				UE_CLOG(bDiagnostics, LogTemp, Log,
				        TEXT("VRgrid ped: #%d steps off the kerb at %.0fm "
				             "(y %.1f -> %.1f)"),
				        P.Index, P.AlongM, P.LateralM, P.CrossTarget);
			}
		}

		if (!Pedestrians.IsValidIndex(P.Index) || Pedestrians[P.Index] == nullptr)
		{
			continue;
		}
		USkeletalMeshComponent* Mesh = Pedestrians[P.Index];

		// Pavement is 20 cm proud of the road; in the carriageway they stand
		// on the tarmac. Height comes from the route, so they walk up the
		// hills with it rather than through them.
		const double Z = (FMath::Abs(P.LateralM) > RoadWidthM * 0.5f) ? 0.20 : 0.0;
		FVector PPos; float PYaw;
		PathAt(P.AlongM, P.LateralM, PPos, PYaw);
		Mesh->SetWorldLocation(PPos + FVector(0, 0, Z * M));

		// ⚑ NO MORE SINE BOB. A 3 cm vertical wobble was standing in for a
		//   walk cycle -- written before the animation was wired and never
		//   removed -- and it ran whether or not the person was moving, so
		//   someone waiting at the kerb hovered on the spot.

		// What is this one ACTUALLY doing on the ground this frame?
		const float GroundSpeed = (P.CrossTarget != 0.0f)
			? CrossSpeedMS                   // crossing: sideways, at the crossing pace
			: FMath::Abs(P.SpeedMS);         // strolling along the pavement

		// ⚑ STRIDE FOLLOWS GROUND SPEED, which is what stops the feet
		//   sliding. The rate was a fixed random 0.85-1.15 regardless of how
		//   fast the person was travelling, so everyone skated a little and
		//   the slow ones skated a lot. MF_Unarmed_Walk_Fwd covers roughly
		//   1.4 m/s at rate 1.
		constexpr float AnimSpeedMS = 1.4f;
		if (GroundSpeed < 0.12f)
		{
			Mesh->SetPlayRate(0.0f);         // standing: hold the pose
		}
		else
		{
			Mesh->SetPlayRate(FMath::Clamp(GroundSpeed / AnimSpeedMS, 0.35f, 1.7f));
		}

		// Face the way they are going, and TURN to it. Assigning the yaw
		// outright span the model through ninety degrees in one frame every
		// time somebody stepped off a kerb.
		// Facing is relative to the ROUTE, not the world, or everyone walks
		// north regardless of which way the street is pointing.
		const float TravelYaw = PYaw + ((P.CrossTarget != 0.0f)
			? ((P.CrossTarget > 0.0f) ? 90.0f : -90.0f)
			: ((P.SpeedMS >= 0.0f) ? 0.0f : 180.0f));
		const float DesiredYaw = TravelYaw + PedMeshYawOffset;
		Mesh->SetWorldRotation(FMath::RInterpTo(
			Mesh->GetComponentRotation(), FRotator(0.0f, DesiredYaw, 0.0f),
			DeltaSeconds, 7.0f));
	}

	// Breathe the bands. A flat wash of colour reads as paint on the road; a
	// slow pulse reads as something being measured. Only 17 instances, so the
	// per-frame custom-data write costs nothing.
	if (false && MapRings != nullptr && BandPulse > 0.0f && BandColours.Num() > 0
	    && !bBlindSpotOnly)
	{
		const float Wave = 1.0f - BandPulse * 0.5f
			+ BandPulse * 0.5f * FMath::Sin(Elapsed * 1.7f);
		for (int32 i = 0; i < BandColours.Num(); ++i)
		{
			// Grain: a cheap hash of band index and time. Each band drifts on
			// its own, so they do not breathe in unison -- instrument noise
			// rather than a strobe.
			const int32 Band = i / 4;
			const float Grain = BandGrain *
				FMath::Sin(Elapsed * (3.1f + Band * 1.7f) + Band * 2.3f) *
				FMath::Sin(Elapsed * (7.9f - Band * 0.9f));
			const float A = FMath::Clamp(BandOpacity * (Wave + Grain), 0.02f, 1.0f);
			const float Data[4] = {BandColours[i].R, BandColours[i].G,
			                       BandColours[i].B, A};
			MapRings->SetCustomData(i, TArrayView<const float>(Data, 4), false);
		}
		MapRings->MarkRenderStateDirty();
	}

	// The map hangs off the car, so the anchor is set after the car has moved
	// and before the map is rebuilt.
	AnchorLocation = CarPos;
	AnchorYawDeg = Yaw;
	AdvanceMap(DeltaSeconds);
	if (MapSweep != nullptr)
	{
		MapSweep->SetWorldLocation(AnchorLocation);
		MapSweep->SetWorldRotation(FRotator(0.0, AnchorYawDeg, 0.0));
	}
	// ⚑ ONE sweep position per frame, and everything reads it.
	//   The drawn rectangle and the red highlights each derived it from
	//   Elapsed independently -- identical arithmetic, and therefore in sync
	//   only until one of them is edited. Now there is a single source.
	MapMaxRangeM = 0.0;
	for (const FVrgRing& Ring : MapScene.Rings)
	{
		MapMaxRangeM = FMath::Max(MapMaxRangeM, static_cast<double>(Ring.HalfWidthM));
	}
	const float SweepPeriod = FMath::Max(SweepPeriodS, 0.2f);
	SweepHalfWidthM = (FMath::Fmod(Elapsed, SweepPeriod) / SweepPeriod) * MapMaxRangeM;

	UpdateBands();
	UpdateSweep(DeltaSeconds);
	UpdateObjectSkins();

	// ⚑ THE CAMERA RIDES THE ROUTE, not a world-space offset.
	//
	//   It used to sit at CarPos + back*distance and look 12 m ahead of the
	//   car. Two things went wrong with that on a real drive: through a bend
	//   the offset points off the road, and on a crest or a dip "12 m ahead"
	//   aims at sky or tarmac -- either way the car leaves frame. Putting the
	//   camera at the arc position BEHIND the car means it follows every turn
	//   and every gradient the road does, because it is on the road.
	FVector CamAnchor; float CamYaw;
	PathAt(DistanceM - CameraBackM, LaneY * 0.5f, CamAnchor, CamYaw);
	const FVector Target = CamAnchor + FVector(0, 0, CameraUpM * M);

	if (!bCameraPlaced)
	{
		CameraLocation = Target;
		bCameraPlaced = true;
	}
	else
	{
		// Wrapping the route teleports the car; snap rather than fly the
		// length of the drive backwards.
		CameraLocation = (FVector::Dist(CameraLocation, Target) > 60.0 * M)
			? Target
			: FMath::Lerp(CameraLocation, Target, 0.22);
	}
	Camera->SetWorldLocation(CameraLocation);

	// Aim at the car, with only a short lead. A long lead is what threw the
	// framing on crests.
	const FVector LookAt = CarPos
		+ FRotator(0.0, Yaw, 0.0).RotateVector(FVector(4.0 * M, 0, 0))
		+ FVector(0, 0, 0.8 * M);
	FRotator Look = (LookAt - CameraLocation).Rotation();
	Look.Roll = 0.0f;          // a chase camera does not bank
	Camera->SetWorldRotation(Look);

	// A single frame says nothing about a 3.7 km drive, so the hook repeats:
	// one shot at -VrgShot=N, then one a minute after, for as long as the run
	// lasts. Checking a change over four points of the route costs one run.
	if (ShotAtFrame >= 0 && Elapsed > ShotAtFrame * 0.1f + ShotsTaken * 55.0f)
	{
		++ShotsTaken;
		bShotTaken = true;
		{
			const FVector SunFwd = MoonLight->GetForwardVector();
			const double Elev = FMath::RadiansToDegrees(FMath::Asin(-SunFwd.Z));
			const FVector Feet = Car->GetComponentLocation();
			const FBoxSphereBounds CB = Car->Bounds;
			const double RoadZ = GroundZAt(Feet, Feet.Z);
			UE_LOG(LogTemp, Log,
			       TEXT("VRgrid shadow: daytime=%d sun fwd=(%.2f %.2f %.2f) elev=%.1fdeg "
			            "| car origin Z=%.1f road Z=%.1f float=%.2f m "
			            "| car bounds minZ=%.1f (%.2f m over road) height=%.2f m "
			            "| predicted offset for that height=%.2f m"),
			       bDaytime ? 1 : 0, SunFwd.X, SunFwd.Y, SunFwd.Z, Elev,
			       Feet.Z, RoadZ, (Feet.Z - RoadZ) / M,
			       CB.Origin.Z - CB.BoxExtent.Z,
			       (CB.Origin.Z - CB.BoxExtent.Z - RoadZ) / M,
			       (CB.BoxExtent.Z * 2.0) / M,
			       ((CB.Origin.Z - CB.BoxExtent.Z - RoadZ) / M)
			           / FMath::Max(FMath::Tan(FMath::DegreesToRadians(Elev)), 0.01));
		}
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(),
		                                     TEXT("Screenshots"), TEXT("vrgrid_sim.png"));
		UE_LOG(LogTemp, Log, TEXT("VRgrid sim: screenshot %d at t=%.0fs -> %s"),
		       ShotsTaken, Elapsed, *Path);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->Viewport->TakeHighResScreenShot();
		}
		FScreenshotRequest::RequestScreenshot(Path, false, false);
	}
}
