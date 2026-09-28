// scene.json -- what the exporter wrote about the run, plus the self-check
// that catches a mirrored map at load instead of on stage.

#pragma once

#include "CoreMinimal.h"

/** One entry of the schedule: ring index, half-width, cell size. */
struct FVrgRing
{
	int32 Ring = 0;
	float HalfWidthM = 0.0f;
	float CellM = 0.0f;
	int64 Cells = 0;
};

struct VRGRIDVIEWER_API FVrgScene
{
	FString SceneName;
	FString Sequence;
	FString ColorBy;
	bool bGhostRemoval = true;
	bool bFeatures = false;

	int32 MapInterval = 5;
	float PlaybackFps = 10.0f;
	int32 FrameFirst = 0;
	int32 FrameLast = 0;
	int32 FrameCount = 0;

	FString ScheduleName;
	float BaseCellM = 0.05f;
	int64 TotalCells = 0;
	int32 CellBytes = 12;
	float MapMB = 0.0f;
	float BlindConeM = 3.74f;
	TArray<FVrgRing> Rings;

	/** Directory holding scene.json, frames/ and stats.jsonl. */
	FString RootDir;

	/** Parse scene.json. Also runs ValidateConversion. */
	static bool Load(const FString& SceneDir, FVrgScene& OutScene, FString& OutError);

	/** frames/%06d.vrgf for a sequence frame index. */
	FString FramePath(int32 FrameIndex) const;

	/** The final-state frame the exporter writes after the loop. */
	FString FinalFramePath() const;

	/**
	 * Run our own conversion over the vectors scene.json carries and compare.
	 *
	 * This exists because a mirrored map is invisible by inspection -- it is a
	 * plausible scene, just the wrong one -- and the y negation is a single
	 * character. The exporter writes what it believes the answer is; this
	 * checks that this build agrees, at load, and refuses rather than
	 * rendering something that will be found by a panelist.
	 */
	static bool ValidateConversion(const TSharedPtr<class FJsonObject>& Root, FString& OutError);
};
