// The simulation window: a night city street with a car driving down it.
//
// THIS IS NOT THE MAP, AND IT IS NOT DERIVED FROM THE DATA. It is an
// illustrative driving scenario that runs beside the Rerun window so a viewer
// can see the kind of environment the system is for. Nothing here is measured,
// nothing here feeds the pipeline, and no number in the deck comes from it.
// Say that out loud when presenting; the two windows show different worlds on
// purpose, and volunteering it costs nothing.
//
// Built entirely in code. The engine ships no city art, so the street is boxes
// -- but a night street IS mostly boxes plus light, and thousands of lit
// windows plus streetlights, headlights and fog carry the read that textures
// would otherwise have to. The one real asset is the car: `SKM_SportsCar` from
// the ChaosModularVehicleExamples plugin, which is fully textured.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VrgFrameReader.h"
#include "VrgScene.h"

#include "VrgSimActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class USkeletalMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class USpotLightComponent;
class UProceduralMeshComponent;

UCLASS()
class VRGRIDVIEWER_API AVrgSimActor : public AActor
{
	GENERATED_BODY()

public:
	AVrgSimActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	/** Length of the route, metres. Overwritten by the real path when one is
	 *  loaded; the 900 m straight is only the fallback. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float StreetLengthM = 900.0f;

	/** Lay the street along the REAL vehicle trajectory from the export.
	 *
	 *  Seq 00 is 3.7 km with 5,158 degrees of cumulative turning and 14.7 m of
	 *  elevation. A straight 900 m loop threw all of that away and made the
	 *  drive look like a treadmill. Every piece of the world -- road, kerbs,
	 *  pavements, buildings, lamps, traffic, people -- is placed by arc length
	 *  along that path and a lateral offset from it, so turns and hills come
	 *  through for free. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	bool bFollowRealPath = true;

	/** Spacing of road segments along the path, metres. Smaller follows tight
	 *  turns more closely and costs more instances. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float RoadSegmentM = 1.6f;

	/** Metres between buildings along each side. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float BuildingSpacingM = 34.0f;

	/** Carriageway width, metres (two lanes plus parking). */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float RoadWidthM = 14.0f;

	/** Ego speed, m/s. 13.9 m/s is 50 km/h -- an urban limit, and close to the
	 *  speeds seq 00 is driven at. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	/** Cruise speed, m/s.
	 *
	 *  ⚑ 8.2 IS THE DATASET'S OWN MEAN, and it is not a style choice.
	 *    At the old 13.9 the recorded yield is physically unreachable: braking
	 *    from 13.9 to 3.4 at 4.5 m/s2 needs 20.2 m, and a pedestrian is not
	 *    detected until 16 m. The car could only ever arrive at cruise or
	 *    slam to a halt -- which is exactly why the yield had a zero floor.
	 *    At 8.2 the same stop needs 6.2 m and fits comfortably.
	 *
	 *    It also very nearly fixes the clock: 3709.5 m at 8.2 m/s is 452 s
	 *    against the recording's 454.0 s. This reproduces the drive's AVERAGE
	 *    pace, not its instantaneous profile -- the real vehicle varied from 0
	 *    to 13.8 m/s. Driving DistanceM straight off the per-frame trajectory
	 *    would reproduce that too; this is the cheap 99% of it. */
	float SpeedMS = 8.2f;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	int32 BlocksPerSide = 18;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	int32 RandomSeed = 26053;

	/** Metres of clear road the ego keeps to the car in front. Below this it
	 *  brakes to match; it does not drive through. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float FollowGapM = 12.0f;

	/** How much road-surface and facade imperfection to add. 0 = showroom
	 *  clean, 1 = patched tarmac, dead lamps, gap sites. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float Grit = 0.7f;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	int32 PedestrianCount = 26;

	/** Put one pedestrian across the car's path on a timer.
	 *
	 *  The ambient crossers wander the whole 900 m loop, so whether one lands
	 *  in front of the car when a panel is watching is luck. This one is
	 *  scheduled: it steps off the far kerb far enough ahead that it reaches
	 *  the ego's lane just as the car arrives. Press N to trigger it by hand. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	bool bScriptedCross = true;

	/** WHERE on the route the scheduled crossing happens, metres along.
	 *
	 *  ⚑ THIS IS A MEASUREMENT, NOT A SETTING.
	 *    In the whole of seq 00 -- 3709.5 m, 454 s -- there is exactly ONE
	 *    clean lateral pedestrian crossing, at frames 4377-4412, which is
	 *    3581 m along the route. The drive contains one stop of any kind
	 *    (2.6 s at 374 m) and it was not for a person.
	 *
	 *    This used to fire on a 26-SECOND TIMER, which walked someone into
	 *    the car's path about ten times a lap and made the sim look like it
	 *    was crawling through a crowd. That behaviour was authored here; it
	 *    was never in the data. One crossing per lap, where the real one
	 *    happened, is the drive rather than an impression of it. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float ScriptedCrossAtM = 3581.0f;

	/** Lateral pace of a crossing pedestrian, m/s. Also what the lead
	 *  distance is computed from, so the two cannot drift apart.
	 *
	 *  2.6 m/s because that is what the recorded one did: 9.6 m of lateral
	 *  travel (y +4.7 -> -4.9) in 3.6 s. A purposeful stride, not a stroll. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float CrossSpeedMS = 2.6f;

	/** Ambient pedestrians keep to the PAVEMENTS.
	 *
	 *  They used to cross the carriageway on 8-26 s dwells, 26 of them, on
	 *  top of the scheduled crossing. Between the two the car met somebody in
	 *  its lane almost continuously. The recorded drive has one crossing, so
	 *  everyone else stays on the footway. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	bool bAmbientPedsCross = false;

	/** What the car slows TO for a pedestrian in its lane, m/s.
	 *
	 *  ⚑ 3.4, not 0. The recorded ego eased 7.88 -> 3.44 m/s for its one
	 *    crossing and never came to a halt. A full stop is not in this data:
	 *    it was authored here, and it is what made the sim stop over and over.
	 *
	 *    The floor was zero for a real reason -- an earlier 3.2 m/s floor let
	 *    the car arrive at somebody still mid-road and drive through them. The
	 *    fix is not a hard stop, it is giving the crosser time to CLEAR:
	 *    `TriggerScriptedCross` now leads by the time to cross the whole road,
	 *    not the time to reach the lane. `YieldPanicGapM` stays as a net. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float YieldFloorMS = 3.4f;

	/** Last resort. Inside this gap the car stops outright, whatever the
	 *  floor says. With the lead distance right this never fires -- the
	 *  collision counters are there to prove it -- but the alternative to a
	 *  net is driving through someone. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float YieldPanicGapM = 2.5f;

	/** Inside this gap the car is at the yield floor, metres. Above it the
	 *  car eases down toward the floor rather than slamming. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float YieldStopGapM = 7.5f;

	/** Half-width of the lane a pedestrian has to be inside to count, metres.
	 *  Roughly a lane plus a body. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float LaneHalfWidthM = 2.2f;

	/** Where the running lane sits, metres right of the centre line.
	 *
	 *  Was RoadWidth/4 = 3.5 m, which put the car 2.45 m from the centre of
	 *  the parked row -- about half a metre of paint-to-paint clearance, and
	 *  it read as clipping them. Pulled in towards the middle of the road. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float EgoLaneM = 2.3f;

	/** Yaw correction for the character mesh, degrees.
	 *
	 *  The UE mannequin is authored facing +Y in its own space -- which is
	 *  exactly why `ACharacter` rotates its mesh component by -90, so that the
	 *  model lines up with the actor's +X forward. Driving these meshes
	 *  directly, without that correction, every pedestrian walked at ninety
	 *  degrees to the way they were travelling. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float PedMeshYawOffset = -90.0f;

	/** Daylight. The night build hid the lack of surface textures behind
	 *  emissive windows and fog, which is exactly what made it read as a game.
	 *  Daylight has nowhere to hide, so the facades carry real depth instead. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	bool bDaytime = true;

	/** Moving vehicles ahead of the ego. Kept low on purpose -- the street
	 *  should look occupied, not like a traffic jam. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	int32 MovingCars = 3;

	/** Draw the real exported map over the street. `-VrgScene=<dir>` turns it
	 *  on; without one the sim runs as a plain driving scene. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowMap = true;

	/** Lift the observed surface this far off the road.
	 *
	 *  Above the flat bands, not just above the tarmac. The bands fill to
	 *  10-12 cm, so at 6 cm every ground cell was drawn UNDERNEATH them and
	 *  the drape was invisible on flat ground -- only cells on raised objects
	 *  poked through. Now the surface reads on top, and the band shows where
	 *  the sensor COULD see but has not yet. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float MapLiftM = 0.15f;

	/** Keep every Nth sweep return. The full 124k points per frame is a lot to
	 *  rebuild twice a second on top of a lit scene; 2 still reads as dense. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	int32 PointStride = 4;

	/** Cells are drawn at this fraction of their true size. Below 1 the gaps
	 *  between them let the street show through, which is the difference
	 *  between an overlay and a blanket. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float CellFill = 1.0f;

	/** Cell height, metres. The map is 2.5D -- a cell is a surface sample, not
	 *  a cube of occupied space -- so drawing it as a TILE is both truer to
	 *  the representation and lets the world stay visible underneath. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float CellThicknessM = 0.06f;

	/** Only draw map within this radius of the car, metres. The full 100 m
	 *  ring reaches the horizon and closes the street in. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float MapDrawRadiusM = 42.0f;

	/** Opacity of the observed surface. Higher than the flat band underneath:
	 *  the band is where the sensor COULD see, the surface is what it did. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float CellOpacity = 0.88f;

	/** Colour the observed surface by which accuracy band it falls in, so the
	 *  skin over an object matches the ring it was measured at. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bCellsUseBandColour = true;

	/** Colour an object is highlighted with as the sweep crosses it. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	FLinearColor ScanColour = FLinearColor(1.0f, 0.10f, 0.10f);

	/** Peak opacity of the scan shell as the sweep passes. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float ScanOpacity = 0.85f;

	/** Resting opacity once the sweep has moved on. 0 leaves the object
	 *  untouched between passes. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float ScanRestOpacity = 0.20f;

	/** How far either side of the sweep edge an object still counts as being
	 *  hit, metres. Wider is a softer, more readable pulse. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float ScanFalloffM = 5.0f;

	/** Lift the band over the objects in THIS street.
	 *
	 *  The exported cells are a drive through Karlsruhe: they climb over the
	 *  cars and kerbs that drive saw, which sit nowhere near the cars in this
	 *  procedural street, so on screen the surface floats past the traffic
	 *  instead of covering it. Since the sim carries no claim about accuracy,
	 *  the honest and far more legible thing is to skin the objects that are
	 *  actually here -- each one capped at its own height, in the colour of
	 *  the band it stands in. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bSkinSimObjects = true;

	/** Emissive gain for the map layers.
	 *
	 *  ⚑ ABOVE 1 ON PURPOSE. These layers are unlit, so their colour is a
	 *    fixed emissive value while the street around them is lit by a 5-lux
	 *    sun. At 0.85 the map was physically present -- right size, right
	 *    height, right material, visible flag set -- and still read as a faint
	 *    grey wash, because the sunlit asphalt was simply brighter. Night did
	 *    not need this; daylight does. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float MapColourScale = 2.4f;

	/** Opacity of the map cells. The whole point of the overlay is that the
	 *  street stays visible underneath it, and at 1.0 the occupied surface is
	 *  a carpet that hides the thing it is supposed to be measuring. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float MapOpacity = 0.55f;

	/** Sigma at which a cell is drawn at its widest, centimetres. Cells at or
	 *  above this are as uncertain as the view bothers to distinguish. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float SigmaFullCm = 14.0f;

	/** Circle radius as a fraction of the cell footprint, at best and worst
	 *  accuracy. A confident cell is a small tight disc; an uncertain one
	 *  swells and fades. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float RadiusAtBest = 0.34f;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float RadiusAtWorst = 1.05f;

	/** The live sweep. Back on, and drawn as circles like everything else --
	 *  it is the returns arriving this frame, and without it the map looks
	 *  static. Sparser than the cells so it reads as a second layer. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowSweep = false;

	/** Colour the cells by ACCURACY rather than by the exported elevation
	 *  ramp. A street is flat, so the height ramp maps almost every cell to
	 *  the same colour and the circles carry no information beyond position.
	 *  Sigma does: confident cells read cool, uncertain ones warm.
	 *
	 *  This is a deliberate divergence from the Rerun window's palette, and
	 *  the only one -- say so if anyone asks which is which. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bColourByAccuracy = true;

	/** The observed-but-free and observed-but-unknown layers. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowFreeUnknown = false;

	/** The filled occupied-cell surface.
	 *
	 *  OFF. At 5 cm near the car this is ~130,000 cells inside 18 m, and no
	 *  amount of thinning stops that reading as a carpet laid over the street.
	 *  The RINGS carry the same argument -- resolution by range -- in four
	 *  lines instead of six figures of geometry. Turn it on to show the actual
	 *  occupied surface; leave it off for the wide shot. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowCells = false;

	/** Concentric accuracy rings on the road: one per schedule ring, at its
	 *  real half-width, coloured by the cell size it buys. This is the
	 *  headline picture -- 5 cm to 10 m, 10 cm to 25, 20 cm to 50, 40 cm to
	 *  100 -- and it is four circles, not a point cloud. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowRings = true;

	/** SQUARE bands, matching the Chebyshev lattice.
	 *
	 *  This is the literal shape of a VRgrid ring: `i_L = i_fine / k_L` on a
	 *  square lattice, so the boundary between resolutions is a square, not a
	 *  circle. It also removes every seam -- a square band is exactly four
	 *  rectangles, where a round one needs hundreds of tiles that cannot
	 *  follow a curve without leaving edges between them. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bSquareBands = true;

	/** Show only the blind spot, or every accuracy band. Toggle with B. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bBlindSpotOnly = false;

	/** Breathe the band opacity so it reads as a live readout rather than
	 *  paint on the road. 0 is flat. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float BandPulse = 0.18f;

	/** A range sweep travelling outward from the car, as a radar or a lidar
	 *  return plot does. This is what stops the bands looking painted on. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowSweepLine = true;

	/** How long the sweep takes to run from the car to the outer ring. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float SweepPeriodS = 2.4f;

	/** Thickness of the travelling edge, metres. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float SweepWidthM = 1.6f;

	/** Per-band brightness jitter. Small: this is instrument noise, not a
	 *  strobe, and a band that flickers hard reads as broken rather than
	 *  live. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float BandGrain = 0.07f;

	/** Opacity of the filled accuracy bands. The road has to stay visible
	 *  underneath, or the overlay stops being an overlay.
	 *
	 *  ⚑ The band is UNLIT EMISSIVE, so the result is
	 *  `a*band + (1-a)*scene` -- and asphalt is dark (0.055) while the
	 *  footway is pale (0.24). At 0.30 the wash therefore swallowed the
	 *  carriageway while leaving the pavement readable, which is exactly
	 *  backwards: the road looked like a sheet of coloured glass and the
	 *  markings disappeared under it. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float BandOpacity = 0.19f;

	/** The 3.74 m blind cone, as a filled disc under the car. Unknown, never
	 *  free -- and the one piece of ground the sensor cannot see at all. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	bool bShowBlindCone = true;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Map")
	float PointSizeM = 0.07f;

	/** Chase camera, metres behind and above the car. */
	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float CameraBackM = 17.0f;

	UPROPERTY(EditAnywhere, Category = "VRgrid|Sim")
	float CameraUpM = 11.5f;

private:
	void BuildLighting();
	void BuildStreet();
	void BuildBuildings();
	void AddFacadeAt(const FVector& FacePos, float Yaw, float Width,
	                 float Height, float Side, FRandomStream& Rng);
	void BuildStreetFurniture();

	/** The carriageway and its footways as CONTINUOUS RIBBONS.
	 *
	 *  A row of rotated boxes can never be a smooth road: butt-joined they
	 *  leave wedges on the outside of every bend, overlapped they z-fight, and
	 *  staggered to stop the fighting they show a step at each joint. All
	 *  three read as "rectangle parts". One mesh whose vertices follow the
	 *  route has no joints at all. */
	void BuildRoadRibbons();

	/** One ribbon: a strip between two lateral offsets, at a height offset. */
	void AddRibbon(int32 Section, float LatA, float LatB, float ZOffsetM,
	               const FLinearColor& Colour, float Roughness,
	               bool bStopAtJunctions);

	/** Load the driven path out of the export's final frame. */
	bool LoadPath();

	/** Is this spot clear of the route everywhere EXCEPT around arc length
	 *  `OwnS`? The drive loops through a town and passes close to itself, so a
	 *  building extruded beside one stretch lands in the middle of another. */
	/** Does another part of the drive CROSS here?
	 *
	 *  ⚑ Not "is another part of the drive nearby" -- that is a different
	 *    question and it was the wrong one. Seq 00 is a loop-closure sequence:
	 *    it re-drives the same streets, so 41%% of the route has another pass
	 *    within 11 m. Treating all of that as junction cut the footway away
	 *    over half the drive. A re-drive is PARALLEL or ANTI-PARALLEL; a real
	 *    junction meets at an angle. Filtering on heading takes it to 17%%,
	 *    which is what an urban loop actually has. */
	bool CrossesRouteAt(const FVector& WorldPos, float OwnS, float OwnYawDeg,
	                    float MinDistM, float IgnoreSpanM) const;

	bool ClearOfRoute(const FVector& WorldPos, float OwnS, float MinDistM,
	                  float IgnoreSpanM) const;

	/** Does the route keep `MinDistM` clear of this FOOTPRINT?
	 *
	 *  Measured to the box, not its centre, and with no arc-length exemption:
	 *  a building must be clear of every part of the drive including its own
	 *  stretch, which it naturally is when it sits behind the pavement. The
	 *  centre-distance test it replaces needed an exemption window, and a
	 *  tight bend that curled back within that window put a building in the
	 *  middle of the road. */
	bool RouteClearsFootprint(const FVector& Centre, float YawDeg,
	                          float WidthM, float DepthM, float MinDistM) const;

	/** Arc length + lateral offset -> world position and heading.
	 *  The single place the route's shape is turned into a location; every
	 *  actor and every piece of scenery goes through it. */
	void PathAt(float AlongM, float LateralM, FVector& OutPos, float& OutYawDeg) const;

	/** A box placed in world space, yawed to the route. */
	/** How much longer a corridor segment must be to close the wedge that
	 *  opens on the OUTSIDE of a bend. */
	float SeamOverlapM(float S, float Step, float LateralM, float WidthM) const;

	void AddBoxAt(UInstancedStaticMeshComponent* Ism, const FVector& WorldPos,
	              const FVector& SizeM, float YawDeg,
	              const FLinearColor& Colour, float Roughness);
	void SpawnTraffic();
	void SpawnPedestrians();

	// --- the real map, drawn over the street -----------------------------
	void LoadMapScene();
	void AdvanceMap(float DeltaSeconds);
	void ApplyMapFrame(const FVrgFrame& Frame);
	void RebuildMapCells(UInstancedStaticMeshComponent* Ism,
	                     const TArray<FVrgCell>& Cells);
	void RebuildMapPoints(UInstancedStaticMeshComponent* Ism,
	                      const TArray<FVrgPoint>& Points, float SizeM, int32 Stride);
	void BuildRingLines();
	void BindSimInput();
	void ApplyBandVisibility();
	bool bBandGeometryLogged = false;
	int32 ShotsTaken = 0;

	/** `-VrgDiag` turns on the per-second telemetry that was used to chase the
	 *  speed stalls and the pedestrian collisions. Off for a demo: the log is
	 *  noisy and the answers it gives are already in the README. */
	bool bDiagnostics = false;
	/** `-VrgOnlyEgoShadow`: every caster but the ego car silenced. */
	bool bOnlyEgoShadow = false;
	/** Rebuild the accuracy bands so they lie ON the road, following its
	 *  elevation ahead and behind rather than on one flat plane at the car. */
	void UpdateBands();
	/** Ground height under a world XY, from the nearest part of the drive.
	 *  A terrain lookup, NOT a look at where the road goes next. */
	double GroundZAt(const FVector& WorldXY, double FallbackZ) const;
	void UpdateSweep(float DeltaSeconds);
	void UpdateObjectSkins();
	/** Attach a scan shell to one mesh, in its own silhouette. */
	void AttachScanShell(class USkeletalMeshComponent* Mesh);
	void DriveScanShell(class USkeletalMeshComponent* Mesh, double RangeM);
	/** Accuracy colour for something this far from the car. */
	FLinearColor BandColourAtRange(double RangeM) const;
	/** The four rectangles of a square band, appended to Xf. */
	void AppendSquareBand(TArray<FTransform>& Xf, double Inner, double Outer, double Z);
public:
	/** B: blind spot only, or every band. */
	void ToggleBands();
	/** N: send a pedestrian across the car's path now. */
	void TriggerScriptedCross();
	/** Metres of warning the crossing needs at the current speed. */
	float ScriptedLeadM() const;
	/** A 2 cm rise across the whole route, so two passes over the same
	 *  ground are never coplanar. See the definition. */
	double ZRampAt(float AlongM) const;
private:
	/** Shift a crossing point clear of the parked row. */
	float ClearOfParkedCars(float AlongM) const;
public:
private:

	/** KITTI world metres -> the sim world, anchored on the ego car.
	 *
	 *  The export is a drive through Karlsruhe and the street under it is
	 *  procedural, so the two will never share absolute coordinates. What they
	 *  CAN share is the vehicle: strip the KITTI vehicle pose off every point,
	 *  then re-apply the sim car's pose. The map then sits on the car and flows
	 *  past it correctly, and the road in the data lines up with the road under
	 *  it because both are simply "ahead of the car". */
	FVector MapToSim(float X, float Y, float Z) const;

	/** Nearest vehicle ahead of `AlongM` in the same lane, or -1.
	 *  Returns its gap in metres through `OutGapM`. */
	int32 CarAhead(float AlongM, float LateralM, float& OutGapM) const;

	/** A lit box: centre and size in METRES, colour, roughness. */
	void AddBox(UInstancedStaticMeshComponent* Ism, const FVector& CentreM,
	            const FVector& SizeM, const FLinearColor& Colour, float Roughness);

	UInstancedStaticMeshComponent* MakeIsm(FName Name, bool bEmissive);
	UInstancedStaticMeshComponent* MakeMapIsm(FName Name, bool bTranslucent);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Surfaces;   // lit
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Emissives;  // windows, paint

	/** The carriageway, kerbs, footways and edge lines, as continuous
	 *  strips rather than a row of boxes. */
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> RoadMesh;

	/** The accuracy bands, as ONE watertight lattice. Instanced boxes each
	 *  took their height from the terrain under their own centre, so
	 *  neighbours stepped and the road showed through the step. */
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> BandMesh;
	/** Other vehicles, as real car meshes. Boxes read as boxes -- and a box
	 *  in a headlight beam blows out white, which is the first thing the eye
	 *  lands on. The engine ships exactly one car, so every vehicle here is
	 *  the same model, varied by paint colour and scale. */
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> TrafficCars;

	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Car;
	UPROPERTY() TObjectPtr<USpotLightComponent> HeadlightL;
	UPROPERTY() TObjectPtr<USpotLightComponent> HeadlightR;

	UPROPERTY() TObjectPtr<UDirectionalLightComponent> MoonLight;
	/** Shadow fill. The captured-scene sky light does not actually reach the
	 *  faces the sun misses here, so a second, shadowless directional light
	 *  carries the ambient instead of relying on a capture that may be
	 *  empty. */
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> StreetLamps;

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapOccupied;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapFree;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapUnknown;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapPoints;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapGhosts;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapRings;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapBlindCone;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapSweep;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MapObjects;

	/** One dynamic material per skinned object, keyed by the component it
	 *  overlays. Created once -- making a MID per frame would churn. */
	UPROPERTY()
	TMap<TObjectPtr<class USkeletalMeshComponent>,
	     TObjectPtr<class UMaterialInstanceDynamic>> ScanShells;

	/** People on the pavements, and a few crossing. The other window's ghost
	 *  story is about pedestrians and cyclists as much as cars, so a street
	 *  with none of them beside it reads as the wrong scenario. */
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Pedestrians;

	struct FPed
	{
		int32 Index = 0;
		float AlongM = 0.0f;
		float LateralM = 0.0f;
		float SpeedMS = 1.3f;
		float CrossTarget = 0.0f;   // non-zero while crossing the road
		/** Seconds left on the pavement before this one crosses again. A
		 *  crosser used to ping-pong across the carriageway forever, so there
		 *  were always people standing in the road. */
		float DwellS = 0.0f;
		float Phase = 0.0f;
	};
	TArray<FPed> Peds;

	/** Traffic that moves: index into TrafficBodies, plus its speed. */
	struct FTraffic
	{
		int32 Index = 0;          // into TrafficCars
		float AlongM = 0.0f;
		float LateralM = 0.0f;
		float SpeedMS = 0.0f;
		FVector SizeM = FVector::ZeroVector;
	};
	TArray<FTraffic> Traffic;

	float DistanceM = 0.0f;
	FVector CameraLocation = FVector::ZeroVector;
	bool bCameraPlaced = false;
	bool bLoggedFirstFrame = false;
	int32 ShotAtFrame = -1;
	bool bShotTaken = false;
	float Elapsed = 0.0f;
	float EgoSpeedMS = 0.0f;   // actual, after following
	int32 ScriptedPedIndex = INDEX_NONE;
	/** Re-armed once the car is past the crossing point, so the scheduled
	 *  crossing fires once per lap rather than every frame near it. */
	bool bScriptedArmed = true;

	// The map overlay's own state.
	FVrgScene MapScene;
	bool bMapLoaded = false;
	int32 MapFrame = 0;
	float MapAccumulator = 0.0f;
	bool bMapRingsBuilt = false;

	/** The driven route, resampled: world positions (cm), heading (deg) and
	 *  cumulative arc length (m). */
	TArray<FVector> PathPos;
	TArray<float> PathYawDeg;
	TArray<float> PathS;
	float PathLengthM = 0.0f;
	bool bHasPath = false;
	/** The sweep edge's half-width THIS frame, metres.
	 *
	 *  Computed once and read by both the drawn rectangle and the object
	 *  highlights. They used the same formula in two places, which is the same
	 *  number until somebody edits one of them. */
	double SweepHalfWidthM = 0.0;
	/** Outermost ring half-width, cached from the schedule. */
	double MapMaxRangeM = 0.0;
	/** Base colour per band instance, so the pulse has something to
	 *  modulate without re-deriving it every frame. */
	TArray<FLinearColor> BandColours;
	int32 BandInstancesPerRing = 0;
	/** The KITTI vehicle pose of the frame currently on screen -- what
	 *  `MapToSim` strips off. */
	FVector MapVehicleM = FVector::ZeroVector;
	float MapVehicleYawRad = 0.0f;
	/** The sim car pose the map is anchored to this frame. */
	FVector AnchorLocation = FVector::ZeroVector;
	float AnchorYawDeg = 0.0f;
};
