#include "VrgFrameReader.h"

#include "Misc/FileHelper.h"

namespace
{
	/** Read a POD out of the buffer at Offset, advancing it. */
	template <typename T>
	FORCEINLINE T Read(const uint8* Data, int32& Offset)
	{
		T Value;
		FMemory::Memcpy(&Value, Data + Offset, sizeof(T));
		Offset += sizeof(T);
		return Value;
	}

	/**
	 * Chunk ids as a comparable integer.
	 *
	 * Spelled out rather than written as a multi-character literal ('PNTS'):
	 * those are implementation-defined in their byte order, warn under clang,
	 * and would silently reverse if this ever built on a different toolchain.
	 * The bytes are little-endian on disk, so id[0] is the low byte.
	 */
	static constexpr uint32 MakeFourCC(char A, char B, char C, char D)
	{
		return static_cast<uint32>(static_cast<uint8>(A))
		     | (static_cast<uint32>(static_cast<uint8>(B)) << 8)
		     | (static_cast<uint32>(static_cast<uint8>(C)) << 16)
		     | (static_cast<uint32>(static_cast<uint8>(D)) << 24);
	}

	static constexpr uint32 CC_PNTS = MakeFourCC('P', 'N', 'T', 'S');
	static constexpr uint32 CC_GHST = MakeFourCC('G', 'H', 'S', 'T');
	static constexpr uint32 CC_OCCU = MakeFourCC('O', 'C', 'C', 'U');
	static constexpr uint32 CC_FREE = MakeFourCC('F', 'R', 'E', 'E');
	static constexpr uint32 CC_UNKN = MakeFourCC('U', 'N', 'K', 'N');
	static constexpr uint32 CC_CONF = MakeFourCC('C', 'O', 'N', 'F');
	static constexpr uint32 CC_CURB = MakeFourCC('C', 'U', 'R', 'B');
	static constexpr uint32 CC_POTH = MakeFourCC('P', 'O', 'T', 'H');
	static constexpr uint32 CC_TRAJ = MakeFourCC('T', 'R', 'A', 'J');

	/**
	 * Copy one chunk's payload into a typed array.
	 *
	 * The stride check is not ceremony. A stride mismatch means the exporter
	 * and this build disagree about an item layout, and memcpy would happily
	 * produce a plausible-looking map out of misaligned floats -- which is the
	 * class of bug that gets found on stage rather than at load.
	 */
	template <typename T>
	bool CopyChunk(const uint8* Data, int32 Offset, uint32 Count, uint32 Stride,
	               TArray<T>& Out, const TCHAR* ChunkName, FString& OutError)
	{
		if (Stride != sizeof(T))
		{
			OutError = FString::Printf(
				TEXT("chunk %s has stride %u, this build expects %d -- the exporter "
				     "and the viewer disagree about the item layout"),
				ChunkName, Stride, static_cast<int32>(sizeof(T)));
			return false;
		}
		Out.SetNumUninitialized(static_cast<int32>(Count));
		if (Count > 0)
		{
			FMemory::Memcpy(Out.GetData(), Data + Offset, static_cast<SIZE_T>(Count) * Stride);
		}
		return true;
	}
}

bool FVrgFrameReader::LoadFrame(const FString& Path, FVrgFrame& OutFrame, FString& OutError)
{
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *Path))
	{
		OutError = FString::Printf(TEXT("could not read %s"), *Path);
		return false;
	}
	if (Raw.Num() < VrgFormat::HeaderBytes)
	{
		OutError = FString::Printf(TEXT("%s is %d bytes, shorter than a header"),
		                           *Path, Raw.Num());
		return false;
	}

	const uint8* Data = Raw.GetData();
	int32 Offset = 0;

	const uint32 Magic = Read<uint32>(Data, Offset);
	if (Magic != VrgFormat::Magic)
	{
		OutError = FString::Printf(TEXT("%s is not a .vrgf (bad magic)"), *Path);
		return false;
	}

	const uint32 Version = Read<uint32>(Data, Offset);
	if (Version != VrgFormat::Version)
	{
		OutError = FString::Printf(
			TEXT("%s is format version %u; this build speaks %u. Re-export the "
			     "scene, or check out the matching viewer."),
			*Path, Version, VrgFormat::Version);
		return false;
	}

	OutFrame.FrameIndex = Read<int32>(Data, Offset);
	const uint32 ChunkCount = Read<uint32>(Data, Offset);

	const float VX = Read<float>(Data, Offset);
	const float VY = Read<float>(Data, Offset);
	const float VZ = Read<float>(Data, Offset);
	const float YawRad = Read<float>(Data, Offset);
	OutFrame.TimeSeconds = Read<float>(Data, Offset);

	OutFrame.VehicleLocation = ToUnreal(VX, VY, VZ);
	OutFrame.VehicleYawDeg = YawToUnrealDeg(YawRad);

	Offset = VrgFormat::HeaderBytes;   // skip `reserved`

	for (uint32 i = 0; i < ChunkCount; ++i)
	{
		if (Offset + VrgFormat::ChunkHeaderBytes > Raw.Num())
		{
			OutError = FString::Printf(TEXT("%s: truncated at chunk %u of %u"),
			                           *Path, i, ChunkCount);
			return false;
		}

		uint8 Id[4];
		FMemory::Memcpy(Id, Data + Offset, 4);
		int32 ChunkOffset = Offset + 4;
		const uint32 Count = Read<uint32>(Data, ChunkOffset);
		const uint32 Stride = Read<uint32>(Data, ChunkOffset);
		const uint32 ByteLength = Read<uint32>(Data, ChunkOffset);
		const int32 PayloadOffset = Offset + VrgFormat::ChunkHeaderBytes;

		if (PayloadOffset + static_cast<int64>(ByteLength) > Raw.Num())
		{
			OutError = FString::Printf(
				TEXT("%s: chunk %d claims %u bytes but only %d remain"),
				*Path, i, ByteLength, Raw.Num() - PayloadOffset);
			return false;
		}

		const uint32 FourCC = (uint32)Id[0] | ((uint32)Id[1] << 8)
		                    | ((uint32)Id[2] << 16) | ((uint32)Id[3] << 24);

		bool bOk = true;
		switch (FourCC)
		{
		case CC_PNTS:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Points, TEXT("PNTS"), OutError);
			OutFrame.bHasPoints = true;
			break;
		case CC_GHST:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Ghosts, TEXT("GHST"), OutError);
			OutFrame.bHasGhosts = true;
			break;
		case CC_OCCU:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Occupied, TEXT("OCCU"), OutError);
			OutFrame.bHasOccupied = true;
			break;
		case CC_FREE:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Free, TEXT("FREE"), OutError);
			OutFrame.bHasFree = true;
			break;
		case CC_UNKN:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Unknown, TEXT("UNKN"), OutError);
			OutFrame.bHasUnknown = true;
			break;
		case CC_CONF:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Confidence, TEXT("CONF"), OutError);
			OutFrame.bHasConfidence = true;
			break;
		case CC_CURB:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Curbs, TEXT("CURB"), OutError);
			OutFrame.bHasCurbs = true;
			break;
		case CC_POTH:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Potholes, TEXT("POTH"), OutError);
			OutFrame.bHasPotholes = true;
			break;
		case CC_TRAJ:
			bOk = CopyChunk(Data, PayloadOffset, Count, Stride, OutFrame.Trajectory, TEXT("TRAJ"), OutError);
			OutFrame.bHasTrajectory = true;
			break;
		default:
			// Unknown id: step over it by its own length and keep going. This
			// is what lets the exporter add a layer without a C++ rebuild.
			break;
		}

		if (!bOk)
		{
			return false;
		}

		Offset = PayloadOffset + static_cast<int32>(ByteLength);
	}

	return true;
}
