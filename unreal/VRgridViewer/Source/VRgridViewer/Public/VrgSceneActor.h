// The viewer: plays a baked .vrgf scene as instanced geometry, with a chase
// camera and the same layer toggles the Rerun window has.
//
// Drop one into an empty level (or let VrgGameMode spawn it) and point
// SceneDir at a directory the exporter wrote. `-VrgScene=<dir>` on the command
// line overrides it, which is how scripts/play_scene.ps1 launches a scene
// beside the Rerun window without anyone editing the level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VrgFrameReader.h"
#include "VrgScene.h"

#include "VrgSceneActor.generated.h"

class UCameraComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

UENUM()
enum class EVrgLayer : uint8
{
	Points,
	Ghosts,
	Occupied,
	Free,
	Unknown,
	Confidence,
	Curbs,
	Potholes,
	Count UMETA(Hidden)
};

UCLASS()
class VRGRIDVIEWER_API AVrgSceneActor : public AActor
{
	GENERATED_BODY()

public:
	AVrgSceneActor();

	/** Directory holding scene.json / frames/ / stats.jsonl. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRgrid")
	FString SceneDir;

	/** Play on load. Pause with Space; scrub with Left/Right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Playback")
	bool bPlaying = true;

	/** Loop the scene, matching the Rerun blueprint's loop_mode="all". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Playback")
	bool bLoop = true;

	/**
	 * Advance on a FIXED timestep from the manifest's playback_fps, not on real
	 * time. Rerun plays the .rrd at 1/fusion.frame_dt_s (10 Hz on KITTI); a
	 * viewer that advanced on wall-clock delta would drift against it over a
	 * 160-frame scene and the two windows would stop agreeing about which
	 * frame is on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Playback")
	bool bFixedTimestep = true;

	/** Edge length of a rendered LiDAR return, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Render")
	float PointSizeM = 0.06f;

	/** Cells are drawn at this fraction of their true size, so the grid reads
	 *  as a grid rather than as a solid sheet. 1.0 is exact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Render")
	float CellFill = 0.92f;

	/** Camera lag, 0-1 per frame. 1 snaps; lower trails the vehicle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Camera")
	float CameraLag = 0.25f;

	/** Chase camera, behind and above the vehicle, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Camera")
	float CameraBackM = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Camera")
	float CameraUpM = 22.0f;

	/**
	 * Follow the vehicle's POSITION but not its heading.
	 *
	 * Copied from the Rerun blueprint, which learned it the hard way: a camera
	 * bolted to the car's yaw swings on every small heading change between
	 * 10 Hz frames, and a camera with a fixed eye never follows at all -- by
	 * frame 1,000 of seq 00 the car is 370 m from the origin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRgrid|Camera")
	bool bCameraFollowsHeading = false;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/**
	 * Hand the player controller OUR camera component, explicitly.
	 *
	 * `AActor::CalcCamera` only uses a camera component when it finds one that
	 * is ACTIVE; otherwise it silently falls back to the actor's own transform.
	 * This actor's root never moves -- it sits at the world origin, which is
	 * where the vehicle starts and therefore inside the 3.74 m blind cone --
	 * so that fallback aims the view at the one region guaranteed to hold no
	 * returns, and the window is black while every diagnostic reads healthy.
	 */
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	/** Jump to a sequence frame index, clamped to the scene's range. */
	UFUNCTION(BlueprintCallable, Category = "VRgrid")
	void SeekToFrame(int32 FrameIndex);

	UFUNCTION(BlueprintCallable, Category = "VRgrid")
	void StepFrames(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "VRgrid")
	void TogglePlay();

	UFUNCTION(BlueprintCallable, Category = "VRgrid")
	void ToggleLayer(EVrgLayer Layer);

	// `BindKey` takes a no-argument handler, so each key gets a named one.
	// They double as the list of what a presenter can do without a mouse:
	// Space pause, arrows scrub, Home restart, G/P/F/U/C toggle a layer.
	void StepForwardOne();
	void StepBackOne();
	void SeekToStart();
	void ToggleGhosts();
	void TogglePointCloud();
	void ToggleFree();
	void ToggleUnknown();
	void ToggleConfidence();

	/** Current sequence frame index (not the loop counter). */
	UFUNCTION(BlueprintPure, Category = "VRgrid")
	int32 GetCurrentFrame() const { return CurrentFrame; }

	UFUNCTION(BlueprintPure, Category = "VRgrid")
	FString GetStatusLine() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "VRgrid")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "VRgrid")
	TObjectPtr<UCameraComponent> Camera;

	/** One ISM per layer. Index with EVrgLayer. */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> LayerComponents;

	/** Ring boundary squares, four thin boxes per ring, tracking the vehicle. */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RingLines;

	/** The 3.74 m blind cone: unknown, never free. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BlindCone;

	/** The path driven. */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Trajectory;

private:
	void SetupComponents();
	void BindInput();
	void ApplyFrame(const FVrgFrame& Frame);
	void RebuildCells(EVrgLayer Layer, const TArray<FVrgCell>& Cells);
	void RebuildPoints(EVrgLayer Layer, const TArray<FVrgPoint>& Points, float SizeM);
	void RebuildBoxes(EVrgLayer Layer, const TArray<FVrgBox>& Boxes);
	void RebuildRingLines();
	void UpdateCamera(const FVrgFrame& Frame);
	bool LoadAndApply(int32 FrameIndex);

	UInstancedStaticMeshComponent* MakeLayer(FName Name, bool bTranslucent);

	FVrgScene Scene;
	bool bSceneLoaded = false;
	FString LoadError;

	int32 CurrentFrame = 0;
	float Accumulator = 0.0f;
	FVector LastVehicleLocation = FVector::ZeroVector;
	FVector CameraLocation = FVector::ZeroVector;
	bool bCameraPlaced = false;
	bool bRingLinesBuilt = false;
	bool bLoggedFirstFrame = false;

	/** `-VrgShot=N` grabs a screenshot N frames in, then quits. Lets a bake be
	 *  verified without a human watching -- which is the only way to tell a
	 *  black material from a misaimed camera from an empty scene. */
	int32 ShotAtFrame = -1;
	bool bShotTaken = false;
	bool bCalcCameraLogged = false;

	/** Layer visibility, mirroring the Rerun entity panel's eye icons. */
	bool LayerVisible[static_cast<int32>(EVrgLayer::Count)];
};
