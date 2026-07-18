#pragma once

#include "CoreMinimal.h"
#include "arcane_wire_generated.h"
#include "flatbuffers/flatbuffers.h"

namespace ArcaneWireCodec {
// Wire type structs for C++ integration (replaces ArcanePostcard types)

struct FArcaneWireUUID {
  uint64 Lo;
  uint64 Hi;

  FArcaneWireUUID() : Lo(0), Hi(0) {}
  FArcaneWireUUID(uint64 InLo, uint64 InHi) : Lo(InLo), Hi(InHi) {}

  bool operator==(const FArcaneWireUUID &Other) const {
    return Lo == Other.Lo && Hi == Other.Hi;
  }

  // Convert from 16-byte array (little-endian)
  static FArcaneWireUUID FromBytes(const uint8 *Bytes);
  // Convert to 16-byte array (little-endian)
  void ToBytes(uint8 *OutBytes) const;
  // Convert from FString hex representation
  static FArcaneWireUUID FromString(const FString &HexString);
  // Convert to FString hex representation
  FString ToString() const;
};

struct FArcaneWireVec3Q {
  int16 X, Y, Z;

  FArcaneWireVec3Q() : X(0), Y(0), Z(0) {}
  FArcaneWireVec3Q(int16 InX, int16 InY, int16 InZ) : X(InX), Y(InY), Z(InZ) {}

  bool operator==(const FArcaneWireVec3Q &Other) const {
    return X == Other.X && Y == Other.Y && Z == Other.Z;
  }
};

struct FArcaneWireEntityState {
  FArcaneWireUUID EntityId;
  FArcaneWireUUID ClusterId;
  FArcaneWireVec3Q Position;
  FArcaneWireVec3Q Velocity;
  TArray<uint8> UserData;
  uint64 ClientSeq;

  FArcaneWireEntityState() : ClientSeq(0) {}
};

struct FArcaneWirePlayerState {
  FArcaneWireUUID EntityId;
  FArcaneWireVec3Q Position;
  FArcaneWireVec3Q Velocity;
  TArray<uint8> UserData;
  uint64 ClientSeq;

  FArcaneWirePlayerState() : ClientSeq(0) {}
};

struct FArcaneWireGameAction {
  FArcaneWireUUID EntityId;
  FString ActionType;
  TArray<uint8> Payload;

  FArcaneWireGameAction() {}
};

struct FArcaneWireDelta {
  FArcaneWireUUID SourceClusterId;
  int64 Seq;
  uint64 Tick;
  double Timestamp;
  TArray<FArcaneWireEntityState> Updated;
  TArray<FArcaneWireUUID> Removed;

  FArcaneWireDelta() : Seq(0), Tick(0), Timestamp(0.0) {}
};

// Encoding functions

/**
 * Encode a client PlayerState frame as FlatBuffer bytes.
 * @param PlayerState The player state to encode
 * @param OutBytes Output byte array
 * @return true if encoding succeeded
 */
bool EncodeClientFrame(const FArcaneWirePlayerState &PlayerState,
                       TArray<uint8> &OutBytes);

/**
 * Encode a client GameAction frame as FlatBuffer bytes.
 * @param GameAction The game action to encode
 * @param OutBytes Output byte array
 * @return true if encoding succeeded
 */
bool EncodeClientAction(const FArcaneWireGameAction &GameAction,
                        TArray<uint8> &OutBytes);

// Decoding functions

/**
 * Decode a client PlayerState frame from FlatBuffer bytes.
 * @param Bytes Input byte array
 * @param OutPlayerState Output player state
 * @return true if decoding succeeded
 */
bool DecodeClientFrame(const TArray<uint8> &Bytes,
                       FArcaneWirePlayerState &OutPlayerState);

/**
 * Decode a client GameAction frame from FlatBuffer bytes.
 * @param Bytes Input byte array
 * @param OutGameAction Output game action
 * @return true if decoding succeeded
 */
bool DecodeClientAction(const TArray<uint8> &Bytes,
                        FArcaneWireGameAction &OutGameAction);

/**
 * Decode a standalone EntityState from FlatBuffer bytes.
 * Used for chunk-based entity decoding in server delta processing.
 * @param Bytes Input byte array
 * @param OutEntityState Output entity state
 * @return true if decoding succeeded
 */
bool DecodeEntityState(const TArray<uint8> &Bytes,
                       FArcaneWireEntityState &OutEntityState);

/**
 * Decode a server DeltaPayload frame from FlatBuffer bytes.
 * Uses the concatenated-chunks pattern for entity data.
 * @param Bytes Input byte array
 * @param OutDelta Output delta payload
 * @return true if decoding succeeded
 */
bool DecodeServerFrame(const TArray<uint8> &Bytes, FArcaneWireDelta &OutDelta);

// Server-side encoding

/**
 * Encode a server delta with concatenated-chunks entity data.
 * Each entity is pre-encoded as a standalone FlatBuffer, then concatenated
 * with offset indices for per-subscriber fan-out.
 * @param Delta The delta to encode
 * @param OutBytes Output byte array
 * @return true if encoding succeeded
 */
bool EncodeServerDelta(const FArcaneWireDelta &Delta, TArray<uint8> &OutBytes);

// Helper functions

/**
 * Convert ArcaneWire::UUID to FArcaneWireUUID.
 */
inline FArcaneWireUUID UuidFromFlatBuffers(const ArcaneWire::UUID *FbUuid) {
  return FArcaneWireUUID(FbUuid->lo(), FbUuid->hi());
}

/**
 * Convert FArcaneWireUUID to ArcaneWire::UUID.
 */
inline ArcaneWire::UUID UuidToFlatBuffers(const FArcaneWireUUID &Uuid) {
  return ArcaneWire::UUID(Uuid.Lo, Uuid.Hi);
}
} // namespace ArcaneWireCodec
