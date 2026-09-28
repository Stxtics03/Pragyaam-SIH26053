#include "VrgSceneActor.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Engine content, so the project needs no authored mesh to run.
	const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	// Generated once by Content/Python/setup_assets.py. Without them the scene
	// still renders, in flat grey -- per-instance colour needs a material that
	// reads PerInstanceCustomData, and that is a material graph, not code.
	const TCHAR* OpaqueMaterialPath = TEXT("/Game/VRgrid/M_VrgInstanced.M_VrgInstanced");
	const TCHAR* TranslucentMaterialPath =
		TEXT("/Game/VRgrid/M_VrgInstancedTranslucent.M_VrgInstancedTranslucent");
	const TCHAR* FallbackMaterialPath =
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	// `/Engine/BasicShapes/Cube` is 100 uu on a side -- one metre -- so an
	// instance scale of S draws a cube of S metres. That identity is why cell
	// sizes go in as metres with no conversion: the mesh already is the unit.
	constexpr double CubeEdgeMetres = 1.0;

	UStaticMesh* LoadMeshChecked(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (Mesh == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("VRgrid: could not load mesh %s"), Path);
		}
		return Mesh;
	}
}

AVrgSceneActor::AVrgSceneActor()
{
	PrimaryActorTick.bCanEverTick = true;
	for (int32 i = 0; i < static_cast<int32>(EVrgLayer::Count); ++i)
	{
		LayerVisible[i] = true;
	}
	SetupComponents();
}

UInstancedStaticMeshComponent* AVrgSceneActor::MakeLayer(FName Name, bool bTranslucent)
{
	UInstancedStaticMeshComponent* Ism =
		CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	Ism->SetupAttachment(Root);

	Ism->SetStaticMesh(LoadMeshChecked(CubeMeshPath));

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(
		nullptr, bTranslucent ? TranslucentMaterialPath : OpaqueMaterialPath);
	if (Material == nullptr)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, FallbackMaterialPath);
	}
	if (Material != nullptr)
	{
		Ism->SetMaterial(0, Material);
	}

	// RGBA per instance. The material reads these as PerInstanceCustomData
	// 0-3; the colours themselves come from the exporter, which takes them
	// from `dashboard/palettes.py` so the two windows cannot drift apart.
	Ism->NumCustomDataFloats = 4;

	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(false);
	Ism->SetMobility(EComponentMobility::Movable);
	// Hundreds of thousands of instances: nothing here should be paying for
	// navigation, overlap or distance-field bookkeeping.
	Ism->bDisableCollision = true;
	Ism->SetCanEverAffectNavigation(false);
	Ism->bAffectDistanceFieldLighting = false;

	return Ism;
}

void AVrgSceneActor::SetupComponents()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LayerComponents.SetNum(static_cast<int32>(EVrgLayer::Count));
	LayerComponents[static_cast<int32>(EVrgLayer::Points)]     = MakeLayer(TEXT("Points"), false);
	LayerComponents[static_cast<int32>(EVrgLayer::Ghosts)]     = MakeLayer(TEXT("Ghosts"), false);
	LayerComponents[static_cast<int32>(EVrgLayer::Occupied)]   = MakeLayer(TEXT("Occupied"), false);
	LayerComponents[static_cast<int32>(EVrgLayer::Free)]       = MakeLayer(TEXT("Free"), true);
	LayerComponents[static_cast<int32>(EVrgLayer::Unknown)]    = MakeLayer(TEXT("Unknown"), true);
	LayerComponents[static_cast<int32>(EVrgLayer::Confidence)] = MakeLayer(TEXT("Confidence"), true);
	LayerComponents[static_cast<int32>(EVrgLayer::Curbs)]      = MakeLayer(TEXT("Curbs"), false);
	LayerComponents[static_cast<int32>(EVrgLayer::Potholes)]   = MakeLayer(TEXT("Potholes"), false);

	RingLines = MakeLayer(TEXT("RingLines"), false);
	Trajectory = MakeLayer(TEXT("Trajectory"), false);

	BlindCone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlindCone"));
	BlindCone->SetupAttachment(Root);
	BlindCone->SetStaticMesh(LoadMeshChecked(CylinderMeshPath));
	BlindCone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlindCone->SetCastShadow(false);

	// ⚑ NO SPRING ARM. A USpringArmComponent REWRITES its child's transform
	//   every tick, so setting the camera's relative location here was undone
	//   before the first frame was drawn -- with TargetArmLength 0 it parked
	//   the camera exactly on the vehicle, inside the 3.74 m blind cone, which
	//   holds no returns by construction. That was the black screen, and the
	//   diagnostic missed it because it logged the location we had just set
	//   rather than the one the arm left behind. The camera is driven directly
	//   in world space in UpdateCamera instead, where nothing else touches it.
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetAutoActivate(true);
	// Absolute: the geometry layers hang off the same Root, so the camera must
	// not inherit anything that moves with them.
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
}

void AVrgSceneActor::BeginPlay()
{
	Super::BeginPlay();

	// `-VrgScene=<dir>` wins, so a launcher script can pick the scene without
	// anyone opening the level. That is how the demo switches scenes on stage.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("VrgScene="), Override) && !Override.IsEmpty())
	{
		SceneDir = Override;
	}
	if (SceneDir.IsEmpty())
	{
		SceneDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("../scenes/foveation"));
	}
	SceneDir = FPaths::ConvertRelativePathToFull(SceneDir);

	FString ShotSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("VrgShot="), ShotSpec))
	{
		ShotAtFrame = FCString::Atoi(*ShotSpec);
	}

	bSceneLoaded = FVrgScene::Load(SceneDir, Scene, LoadError);
	if (!bSceneLoaded)
	{
		// Loud and specific. A viewer that silently shows an empty world on
		// stage is worse than one that says why in the first line of the log.
		UE_LOG(LogTemp, Error, TEXT("VRgrid: %s"), *LoadError);
		return;
	}

	CurrentFrame = Scene.FrameFirst;
	RebuildRingLines();
	LoadAndApply(CurrentFrame);
	BindInput();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		// ⚑ THIS LINE IS WHY THE WINDOW WAS BLACK, and it has to come first.
		//   With bAutoManageActiveCameraTarget left at its default (true), the
		//   controller re-points the view at its possessed pawn on the next
		//   tick -- so SetViewTarget below appeared to work and was silently
		//   undone, leaving the player inside the point cloud at the origin
		//   looking at nothing. Geometry, materials and visibility were all
		//   correct the whole time; only the camera was wrong.
		PC->bAutoManageActiveCameraTarget = false;
		PC->SetViewTarget(this);

		// Checked AFTER the call, not before. The first version of this log
		// lived in ApplyFrame, which BeginPlay runs earlier -- so it reported
		// the pawn and read as a failure when nothing had been set yet.
		UE_LOG(LogTemp, Log, TEXT("VRgrid: view target is %s (expected %s)"),
		       PC->GetViewTarget() ? *PC->GetViewTarget()->GetName() : TEXT("NULL"),
		       *GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid: no player controller at BeginPlay -- the chase "
		            "camera was never installed and the window will be black."));
	}
}

void AVrgSceneActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (Camera != nullptr)
	{
		if (!bCalcCameraLogged)
		{
			bCalcCameraLogged = true;
			UE_LOG(LogTemp, Log, TEXT("VRgrid: CalcCamera in use, camera at (%.0f %.0f %.0f)"),
			       Camera->GetComponentLocation().X, Camera->GetComponentLocation().Y,
			       Camera->GetComponentLocation().Z);
		}
		Camera->GetCameraView(DeltaTime, OutResult);
		return;
	}
	Super::CalcCamera(DeltaTime, OutResult);
}

void AVrgSceneActor::BindInput()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}
	EnableInput(PC);
	if (InputComponent == nullptr)
	{
		return;
	}

	// Bound to keys directly rather than through Enhanced Input assets: this
	// project ships no .uasset, and a demo should not need one to pause.
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AVrgSceneActor::TogglePlay);
	InputComponent->BindKey(EKeys::Right, IE_Pressed, this,
	                        &AVrgSceneActor::StepForwardOne);
	InputComponent->BindKey(EKeys::Left, IE_Pressed, this,
	                        &AVrgSceneActor::StepBackOne);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AVrgSceneActor::ToggleGhosts);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AVrgSceneActor::ToggleFree);
	InputComponent->BindKey(EKeys::U, IE_Pressed, this, &AVrgSceneActor::ToggleUnknown);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AVrgSceneActor::TogglePointCloud);
	InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AVrgSceneActor::ToggleConfidence);
	InputComponent->BindKey(EKeys::Home, IE_Pressed, this, &AVrgSceneActor::SeekToStart);
}

void AVrgSceneActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSceneLoaded || !bPlaying || Scene.FrameCount <= 0)
	{
		return;
	}

	const float Fps = FMath::Max(Scene.PlaybackFps, 1.0f);
	const float Step = 1.0f / Fps;

	Accumulator += DeltaSeconds;
	while (Accumulator >= Step)
	{
		Accumulator -= Step;
		int32 Next = CurrentFrame + 1;
		if (Next > Scene.FrameLast)
		{
			if (!bLoop)
			{
				bPlaying = false;
				break;
			}
			Next = Scene.FrameFirst;
		}
		CurrentFrame = Next;
		LoadAndApply(CurrentFrame);

		if (!bFixedTimestep)
		{
			// Real-time mode: never advance more than one frame per tick, so a
			// hitch does not fast-forward past the Rerun window.
			Accumulator = 0.0f;
			break;
		}
	}

	if (ShotAtFrame >= 0 && !bShotTaken
	    && CurrentFrame >= Scene.FrameFirst + ShotAtFrame)
	{
		bShotTaken = true;
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(),
		                                     TEXT("Screenshots"), TEXT("vrgrid_shot.png"));
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* Mgr = PC->PlayerCameraManager)
			{
				const FVector V = Mgr->GetCameraLocation();
				const FRotator R = Mgr->GetCameraRotation();
				UE_LOG(LogTemp, Log,
				       TEXT("VRgrid: SETTLED engine view (%.0f %.0f %.0f) rot=(%.1f %.1f %.1f) "
				            "| our camera (%.0f %.0f %.0f) | target=%s"),
				       V.X, V.Y, V.Z, R.Pitch, R.Yaw, R.Roll,
				       Camera->GetComponentLocation().X, Camera->GetComponentLocation().Y,
				       Camera->GetComponentLocation().Z,
				       PC->GetViewTarget() ? *PC->GetViewTarget()->GetName() : TEXT("NULL"));
			}
		}
		UE_LOG(LogTemp, Log, TEXT("VRgrid: screenshot -> %s"), *Path);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->Viewport->TakeHighResScreenShot();
		}
		FScreenshotRequest::RequestScreenshot(Path, false, false);
	}
}

bool AVrgSceneActor::LoadAndApply(int32 FrameIndex)
{
	FVrgFrame Frame;
	FString Error;
	if (!FVrgFrameReader::LoadFrame(Scene.FramePath(FrameIndex), Frame, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("VRgrid: %s"), *Error);
		return false;
	}
	ApplyFrame(Frame);
	return true;
}

void AVrgSceneActor::ApplyFrame(const FVrgFrame& Frame)
{
	// A layer that is PRESENT but empty is cleared; a layer that is ABSENT is
	// left alone. The map is only recomputed every MapInterval frames, so on
	// the frames between, "absent" means "what is on screen is still current".
	// Conflating the two is how a ghost trail outlives the cleanup.
	if (Frame.bHasPoints)
	{
		RebuildPoints(EVrgLayer::Points, Frame.Points, PointSizeM);
	}
	if (Frame.bHasGhosts)
	{
		RebuildPoints(EVrgLayer::Ghosts, Frame.Ghosts, PointSizeM * 1.5f);
	}
	if (Frame.bHasOccupied)
	{
		RebuildCells(EVrgLayer::Occupied, Frame.Occupied);
	}
	if (Frame.bHasFree)
	{
		RebuildCells(EVrgLayer::Free, Frame.Free);
	}
	if (Frame.bHasUnknown)
	{
		RebuildCells(EVrgLayer::Unknown, Frame.Unknown);
	}
	if (Frame.bHasConfidence)
	{
		RebuildCells(EVrgLayer::Confidence, Frame.Confidence);
	}
	if (Frame.bHasCurbs)
	{
		RebuildBoxes(EVrgLayer::Curbs, Frame.Curbs);
	}
	if (Frame.bHasPotholes)
	{
		RebuildBoxes(EVrgLayer::Potholes, Frame.Potholes);
	}

	if (Frame.bHasTrajectory && Trajectory != nullptr)
	{
		Trajectory->ClearInstances();
		TArray<FTransform> Transforms;
		Transforms.Reserve(Frame.Trajectory.Num());
		for (const FVrgVec3& P : Frame.Trajectory)
		{
			Transforms.Emplace(FRotator::ZeroRotator,
			                   FVrgFrameReader::ToUnreal(P.X, P.Y, P.Z),
			                   FVector(0.35, 0.35, 0.05));
		}
		if (Transforms.Num() > 0)
		{
			Trajectory->AddInstances(Transforms, false, true);
		}
	}

	UpdateCamera(Frame);

	// One-shot diagnostic on the first applied frame. A black window has three
	// possible causes -- no instances, no material, or a camera looking the
	// wrong way -- and they are indistinguishable from the outside. This prints
	// which one it is instead of leaving it to be guessed at.
	if (!bLoggedFirstFrame)
	{
		bLoggedFirstFrame = true;
		for (int32 i = 0; i < static_cast<int32>(EVrgLayer::Count); ++i)
		{
			if (UInstancedStaticMeshComponent* Ism = LayerComponents[i])
			{
				UE_LOG(LogTemp, Log,
				       TEXT("VRgrid: layer %d  instances=%d  mesh=%s  material=%s  visible=%d"),
				       i, Ism->GetInstanceCount(),
				       Ism->GetStaticMesh() ? TEXT("ok") : TEXT("NULL"),
				       Ism->GetMaterial(0) ? *Ism->GetMaterial(0)->GetName() : TEXT("NULL"),
				       Ism->IsVisible() ? 1 : 0);
			}
		}
		const FVector CamLoc = Camera ? Camera->GetComponentLocation() : FVector::ZeroVector;
		const FRotator CamRot = Camera ? Camera->GetComponentRotation() : FRotator::ZeroRotator;
		UE_LOG(LogTemp, Log,
		       TEXT("VRgrid: vehicle=(%.0f %.0f %.0f)  camera=(%.0f %.0f %.0f) "
		            "rot=(%.1f %.1f %.1f)  standoff=%.1f m"),
		       Frame.VehicleLocation.X, Frame.VehicleLocation.Y, Frame.VehicleLocation.Z,
		       CamLoc.X, CamLoc.Y, CamLoc.Z, CamRot.Pitch, CamRot.Yaw, CamRot.Roll,
		       FVector::Dist(CamLoc, Frame.VehicleLocation) / 100.0);

	}
}

void AVrgSceneActor::RebuildCells(EVrgLayer Layer, const TArray<FVrgCell>& Cells)
{
	UInstancedStaticMeshComponent* Ism = LayerComponents[static_cast<int32>(Layer)];
	if (Ism == nullptr)
	{
		return;
	}

	Ism->ClearInstances();
	if (Cells.Num() == 0)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Cells.Num());
	for (const FVrgCell& C : Cells)
	{
		// Scale IS the cell size: the unit cube is one metre, and CellFill
		// leaves a hairline between neighbours so the lattice reads as a
		// lattice. This is the foveation -- 5 cm near the car, 40 cm at 100 m.
		const double Edge = static_cast<double>(C.CellM) * CellFill * CubeEdgeMetres;
		Transforms.Emplace(FRotator::ZeroRotator,
		                   FVrgFrameReader::ToUnreal(C.X, C.Y, C.Z),
		                   FVector(Edge, Edge, Edge));
	}
	Ism->AddInstances(Transforms, false, true);

	float Data[4];
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FVrgCell& C = Cells[i];
		FVrgFrameReader::ColorToCustomData(C.R, C.G, C.B, C.A, Data);
		Ism->SetCustomData(i, TArrayView<const float>(Data, 4), /*bMarkRenderStateDirty=*/false);
	}
	Ism->MarkRenderStateDirty();
}

void AVrgSceneActor::RebuildPoints(EVrgLayer Layer, const TArray<FVrgPoint>& Points, float SizeM)
{
	UInstancedStaticMeshComponent* Ism = LayerComponents[static_cast<int32>(Layer)];
	if (Ism == nullptr)
	{
		return;
	}

	Ism->ClearInstances();
	if (Points.Num() == 0)
	{
		return;
	}

	const double Edge = static_cast<double>(SizeM) * CubeEdgeMetres;
	TArray<FTransform> Transforms;
	Transforms.Reserve(Points.Num());
	for (const FVrgPoint& P : Points)
	{
		Transforms.Emplace(FRotator::ZeroRotator,
		                   FVrgFrameReader::ToUnreal(P.X, P.Y, P.Z),
		                   FVector(Edge, Edge, Edge));
	}
	Ism->AddInstances(Transforms, false, true);

	float Data[4];
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		const FVrgPoint& P = Points[i];
		FVrgFrameReader::ColorToCustomData(P.R, P.G, P.B, P.A, Data);
		Ism->SetCustomData(i, TArrayView<const float>(Data, 4), false);
	}
	Ism->MarkRenderStateDirty();
}

void AVrgSceneActor::RebuildBoxes(EVrgLayer Layer, const TArray<FVrgBox>& Boxes)
{
	UInstancedStaticMeshComponent* Ism = LayerComponents[static_cast<int32>(Layer)];
	if (Ism == nullptr)
	{
		return;
	}

	Ism->ClearInstances();
	if (Boxes.Num() == 0)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Boxes.Num());
	for (const FVrgBox& B : Boxes)
	{
		// Half-extents, so the drawn box is twice each. A curb stands UP at its
		// measured rise and a pothole sinks DOWN to its measured depth -- the
		// height is the thing a 2D grid loses, so it is drawn at magnitude.
		Transforms.Emplace(
			FRotator::ZeroRotator,
			FVrgFrameReader::ToUnreal(B.X, B.Y, B.Z),
			FVector(static_cast<double>(B.HX) * 2.0 * CubeEdgeMetres,
			        static_cast<double>(B.HY) * 2.0 * CubeEdgeMetres,
			        static_cast<double>(B.HZ) * 2.0 * CubeEdgeMetres));
	}
	Ism->AddInstances(Transforms, false, true);

	float Data[4];
	for (int32 i = 0; i < Boxes.Num(); ++i)
	{
		const FVrgBox& B = Boxes[i];
		FVrgFrameReader::ColorToCustomData(B.R, B.G, B.B, B.A, Data);
		Ism->SetCustomData(i, TArrayView<const float>(Data, 4), false);
	}
	Ism->MarkRenderStateDirty();
}

void AVrgSceneActor::RebuildRingLines()
{
	if (RingLines == nullptr || bRingLinesBuilt)
	{
		return;
	}

	// The rings are SQUARES, not circles: the lattice is Chebyshev, and a
	// circle here would be a prettier picture of a different structure.
	RingLines->ClearInstances();

	TArray<FTransform> Transforms;
	TArray<FLinearColor> Colors;
	constexpr double LineHalfWidthM = 0.09;

	for (const FVrgRing& Ring : Scene.Rings)
	{
		const double H = Ring.HalfWidthM;
		// Four thin slabs: two along x, two along y. Drawn as geometry rather
		// than debug lines so they survive in a packaged Shipping build.
		const FVector Extents[4] = {
			FVector(LineHalfWidthM * 2.0, H * 2.0, 0.04),
			FVector(LineHalfWidthM * 2.0, H * 2.0, 0.04),
			FVector(H * 2.0, LineHalfWidthM * 2.0, 0.04),
			FVector(H * 2.0, LineHalfWidthM * 2.0, 0.04),
		};
		const FVector Offsets[4] = {
			FVector(H, 0.0, 0.0), FVector(-H, 0.0, 0.0),
			FVector(0.0, H, 0.0), FVector(0.0, -H, 0.0),
		};
		for (int32 i = 0; i < 4; ++i)
		{
			Transforms.Emplace(
				FRotator::ZeroRotator,
				FVrgFrameReader::ToUnreal(Offsets[i].X, Offsets[i].Y, Offsets[i].Z),
				Extents[i]);
		}
	}

	if (Transforms.Num() > 0)
	{
		RingLines->AddInstances(Transforms, false, false);
		// `_RING_LINE_RGB` in pipeline_view.py: (200, 205, 212).
		const float Data[4] = {200.f / 255.f, 205.f / 255.f, 212.f / 255.f, 1.0f};
		for (int32 i = 0; i < Transforms.Num(); ++i)
		{
			RingLines->SetCustomData(i, TArrayView<const float>(Data, 4), false);
		}
		RingLines->MarkRenderStateDirty();
	}

	if (BlindCone != nullptr)
	{
		// `/Engine/BasicShapes/Cylinder` is 100 uu tall and 100 uu across, so a
		// scale of (r, r, t) in metres gives a disc of radius r/2. Doubled so
		// the drawn radius IS blind_cone_m -- 3.74 m, math 1.4 eq (5).
		const double Diameter = static_cast<double>(Scene.BlindConeM) * 2.0;
		BlindCone->SetRelativeScale3D(FVector(Diameter, Diameter, 0.02));
	}

	bRingLinesBuilt = true;
}

void AVrgSceneActor::UpdateCamera(const FVrgFrame& Frame)
{
	LastVehicleLocation = Frame.VehicleLocation;

	// Ring boundaries and the blind cone track the vehicle, exactly as they do
	// under Rerun's `world/vehicle` transform.
	if (RingLines != nullptr)
	{
		RingLines->SetWorldLocation(Frame.VehicleLocation);
	}
	if (BlindCone != nullptr)
	{
		BlindCone->SetWorldLocation(Frame.VehicleLocation);
	}
	if (Camera == nullptr)
	{
		return;
	}

	const double Back = FVrgFrameReader::LengthToUnreal(CameraBackM);
	const double Up = FVrgFrameReader::LengthToUnreal(CameraUpM);

	// Behind and above, in WORLD space. Heading is ignored by default: a camera
	// bolted to the car's yaw swings on every small heading change between
	// 10 Hz frames, which the Rerun blueprint learned the hard way.
	FVector Offset(-Back, 0.0, Up);
	if (bCameraFollowsHeading)
	{
		Offset = FRotator(0.0, Frame.VehicleYawDeg, 0.0).RotateVector(Offset);
	}
	const FVector Target = Frame.VehicleLocation + Offset;

	if (!bCameraPlaced)
	{
		CameraLocation = Target;      // no lag into the first frame
		bCameraPlaced = true;
	}
	else
	{
		CameraLocation = FMath::Lerp(CameraLocation, Target,
		                             FMath::Clamp(CameraLag, 0.01f, 1.0f));
	}

	Camera->SetWorldLocation(CameraLocation);
	// Look AT the vehicle from wherever the lag left us, rather than at a fixed
	// pitch -- during a lag catch-up a fixed pitch drifts off the car.
	Camera->SetWorldRotation(
		(Frame.VehicleLocation - CameraLocation).Rotation());
}

void AVrgSceneActor::SeekToFrame(int32 FrameIndex)
{
	if (!bSceneLoaded)
	{
		return;
	}
	CurrentFrame = FMath::Clamp(FrameIndex, Scene.FrameFirst, Scene.FrameLast);
	Accumulator = 0.0f;
	LoadAndApply(CurrentFrame);
}

void AVrgSceneActor::StepFrames(int32 Delta)
{
	SeekToFrame(CurrentFrame + Delta);
}

void AVrgSceneActor::TogglePlay()
{
	bPlaying = !bPlaying;
}

void AVrgSceneActor::ToggleLayer(EVrgLayer Layer)
{
	const int32 Index = static_cast<int32>(Layer);
	if (!LayerComponents.IsValidIndex(Index) || LayerComponents[Index] == nullptr)
	{
		return;
	}
	LayerVisible[Index] = !LayerVisible[Index];
	LayerComponents[Index]->SetVisibility(LayerVisible[Index]);
}

void AVrgSceneActor::StepForwardOne()
{
	bPlaying = false;          // scrubbing implies paused, as in the viewer
	StepFrames(1);
}

void AVrgSceneActor::StepBackOne()
{
	bPlaying = false;
	// Stepping BACK re-reads an earlier frame, and the map layers on that
	// frame may be absent (they are only written every MapInterval). Walk back
	// to the nearest map frame so the cells on screen belong to where the car
	// is, rather than being the newer map with an older vehicle under it.
	const int32 Target = FMath::Max(CurrentFrame - 1, Scene.FrameFirst);
	const int32 Interval = FMath::Max(Scene.MapInterval, 1);
	const int32 MapFrame = Scene.FrameFirst
		+ ((Target - Scene.FrameFirst) / Interval) * Interval;
	for (int32 F = MapFrame; F <= Target; ++F)
	{
		LoadAndApply(F);
	}
	CurrentFrame = Target;
	Accumulator = 0.0f;
}

void AVrgSceneActor::SeekToStart()
{
	SeekToFrame(Scene.FrameFirst);
}

void AVrgSceneActor::ToggleGhosts()
{
	// The Gate 3 toggle, at the point-cloud level: the same eye icon on
	// `world/ghosts` that the Rerun entity panel has.
	ToggleLayer(EVrgLayer::Ghosts);
}

void AVrgSceneActor::TogglePointCloud()
{
	ToggleLayer(EVrgLayer::Points);
}

void AVrgSceneActor::ToggleFree()
{
	ToggleLayer(EVrgLayer::Free);
}

void AVrgSceneActor::ToggleUnknown()
{
	ToggleLayer(EVrgLayer::Unknown);
}

void AVrgSceneActor::ToggleConfidence()
{
	ToggleLayer(EVrgLayer::Confidence);
}

FString AVrgSceneActor::GetStatusLine() const
{
	if (!bSceneLoaded)
	{
		return FString::Printf(TEXT("VRgrid: NO SCENE -- %s"), *LoadError);
	}
	return FString::Printf(
		TEXT("%s  seq %s  frame %d / %d   %s   %.2f MB fixed   ghosts %s"),
		*Scene.SceneName, *Scene.Sequence, CurrentFrame, Scene.FrameLast,
		bPlaying ? TEXT("playing") : TEXT("paused"), Scene.MapMB,
		Scene.bGhostRemoval ? TEXT("removed") : TEXT("KEPT"));
}
