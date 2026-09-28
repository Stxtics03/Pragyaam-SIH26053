#include "VrgScene.h"

#include "VrgFrameReader.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	float GetNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float Fallback)
	{
		double Value = 0.0;
		return Obj->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : Fallback;
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32 Fallback)
	{
		int32 Value = 0;
		return Obj->TryGetNumberField(Field, Value) ? Value : Fallback;
	}
}

bool FVrgScene::ValidateConversion(const TSharedPtr<FJsonObject>& Root, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Vectors = nullptr;
	if (!Root->TryGetArrayField(TEXT("conversion_test_vectors"), Vectors) || Vectors == nullptr)
	{
		// An older export without the vectors. Say so rather than passing
		// silently -- "no check ran" and "the check passed" are different.
		UE_LOG(LogTemp, Warning,
		       TEXT("VRgrid: scene.json carries no conversion_test_vectors; the "
		            "frame convention was NOT verified. Re-export to get the check."));
		return true;
	}

	// One tolerance, in centimetres, and loose enough that float32 in the
	// manifest is never the reason this fires.
	constexpr double ToleranceCm = 1e-3;

	for (int32 i = 0; i < Vectors->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!(*Vectors)[i]->TryGetObject(Entry) || Entry == nullptr)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* Src = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Dst = nullptr;
		if (!(*Entry)->TryGetArrayField(TEXT("m"), Src) ||
		    !(*Entry)->TryGetArrayField(TEXT("cm"), Dst) ||
		    Src == nullptr || Dst == nullptr ||
		    Src->Num() != 3 || Dst->Num() != 3)
		{
			continue;
		}

		const FVector Ours = FVrgFrameReader::ToUnreal(
			static_cast<float>((*Src)[0]->AsNumber()),
			static_cast<float>((*Src)[1]->AsNumber()),
			static_cast<float>((*Src)[2]->AsNumber()));

		const FVector Theirs(
			(*Dst)[0]->AsNumber(), (*Dst)[1]->AsNumber(), (*Dst)[2]->AsNumber());

		if (!Ours.Equals(Theirs, ToleranceCm))
		{
			OutError = FString::Printf(
				TEXT("FRAME CONVENTION MISMATCH. The exporter says "
				     "(%g, %g, %g) m -> (%g, %g, %g) cm; this build computes "
				     "(%g, %g, %g). VRgrid is x-forward / y-LEFT / z-up in metres "
				     "and Unreal is y-RIGHT in centimetres, so y must be negated. "
				     "Refusing to load -- a mirrored map looks correct."),
				(*Src)[0]->AsNumber(), (*Src)[1]->AsNumber(), (*Src)[2]->AsNumber(),
				Theirs.X, Theirs.Y, Theirs.Z, Ours.X, Ours.Y, Ours.Z);
			return false;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VRgrid: frame convention verified against %d vectors."),
	       Vectors->Num());
	return true;
}

bool FVrgScene::Load(const FString& SceneDir, FVrgScene& OutScene, FString& OutError)
{
	const FString ManifestPath = FPaths::Combine(SceneDir, TEXT("scene.json"));

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		OutError = FString::Printf(
			TEXT("no scene.json at %s -- point VrgSceneDir at a directory the "
			     "exporter wrote (it holds scene.json, frames/ and stats.jsonl)"),
			*ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("%s is not valid JSON"), *ManifestPath);
		return false;
	}

	if (!ValidateConversion(Root, OutError))
	{
		return false;
	}

	OutScene.RootDir = SceneDir;
	Root->TryGetStringField(TEXT("scene"), OutScene.SceneName);
	Root->TryGetStringField(TEXT("sequence"), OutScene.Sequence);
	Root->TryGetStringField(TEXT("color_by"), OutScene.ColorBy);
	Root->TryGetBoolField(TEXT("ghost_removal"), OutScene.bGhostRemoval);
	Root->TryGetBoolField(TEXT("features"), OutScene.bFeatures);

	OutScene.MapInterval = GetInt(Root, TEXT("map_interval"), 5);
	OutScene.PlaybackFps = GetNumber(Root, TEXT("playback_fps"), 10.0f);
	OutScene.FrameFirst = GetInt(Root, TEXT("frame_first"), 0);
	OutScene.FrameLast = GetInt(Root, TEXT("frame_last"), 0);
	OutScene.FrameCount = GetInt(Root, TEXT("frame_count"), 0);
	OutScene.BlindConeM = GetNumber(Root, TEXT("blind_cone_m"), 3.74f);

	const TSharedPtr<FJsonObject>* Schedule = nullptr;
	if (Root->TryGetObjectField(TEXT("schedule"), Schedule) && Schedule != nullptr)
	{
		(*Schedule)->TryGetStringField(TEXT("name"), OutScene.ScheduleName);
		OutScene.BaseCellM = GetNumber(*Schedule, TEXT("base_cell_m"), 0.05f);
		OutScene.CellBytes = GetInt(*Schedule, TEXT("cell_bytes"), 12);
		OutScene.MapMB = GetNumber(*Schedule, TEXT("map_mb"), 0.0f);
		double TotalCells = 0.0;
		if ((*Schedule)->TryGetNumberField(TEXT("total_cells"), TotalCells))
		{
			OutScene.TotalCells = static_cast<int64>(TotalCells);
		}

		const TArray<TSharedPtr<FJsonValue>>* Rings = nullptr;
		if ((*Schedule)->TryGetArrayField(TEXT("rings"), Rings) && Rings != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Rings)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Value->TryGetObject(Obj) || Obj == nullptr)
				{
					continue;
				}
				FVrgRing Ring;
				Ring.Ring = GetInt(*Obj, TEXT("ring"), 0);
				Ring.HalfWidthM = GetNumber(*Obj, TEXT("half_width_m"), 0.0f);
				Ring.CellM = GetNumber(*Obj, TEXT("cell_m"), 0.05f);
				double Cells = 0.0;
				(*Obj)->TryGetNumberField(TEXT("cells"), Cells);
				Ring.Cells = static_cast<int64>(Cells);
				OutScene.Rings.Add(Ring);
			}
		}
	}

	UE_LOG(LogTemp, Log,
	       TEXT("VRgrid: loaded scene '%s' (seq %s), frames %d-%d (%d), "
	            "schedule %s, %.2f MB fixed, blind cone %.2f m"),
	       *OutScene.SceneName, *OutScene.Sequence, OutScene.FrameFirst,
	       OutScene.FrameLast, OutScene.FrameCount, *OutScene.ScheduleName,
	       OutScene.MapMB, OutScene.BlindConeM);

	return true;
}

FString FVrgScene::FramePath(int32 FrameIndex) const
{
	return FPaths::Combine(RootDir, TEXT("frames"),
	                       FString::Printf(TEXT("%06d.vrgf"), FrameIndex));
}

FString FVrgScene::FinalFramePath() const
{
	return FPaths::Combine(RootDir, TEXT("frames"), TEXT("final.vrgf"));
}
