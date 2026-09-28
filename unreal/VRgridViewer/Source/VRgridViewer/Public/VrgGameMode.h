// Spawns the viewer, so the project runs on an engine map with no .uasset of
// its own. The scene comes from `-VrgScene=<dir>`; see AVrgSceneActor.
//
// Deliberately asset-free: this repo ships source only. The moment the viewer
// needs a level or a Blueprint to start, "copy the folder and run it" stops
// being true, and that is the property that makes it safe to hand to someone
// an hour before a demo.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "VrgGameMode.generated.h"

UCLASS()
class VRGRIDVIEWER_API AVrgGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVrgGameMode();

	virtual void StartPlay() override;

private:
	UPROPERTY()
	TObjectPtr<class AVrgSceneActor> SceneActor;
};
