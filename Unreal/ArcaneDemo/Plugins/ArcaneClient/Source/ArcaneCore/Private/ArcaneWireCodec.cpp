#include "ArcaneWireCodec.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogArcaneWireCodec, Log, All);

namespace ArcaneWireCodec {
// UUID conversion utilities

FArcaneWireUUID FArcaneWireUUID::FromBytes(const uint8 *Bytes) {
  uint64 Lo, Hi;
  FMemory::Memcpy(&Lo, Bytes, 8); // Little-endian on x86/ARM
  FMemory::Memcpy(&Hi, Bytes + 8, 8);
  return FArcaneWireUUID(Lo, Hi);
}

void FArcaneWireUUID::ToBytes(uint8 *OutBytes) const {
  FMemory::Memcpy(OutBytes, &Lo, 8);
  FMemory::Memcpy(OutBytes + 8, &Hi, 8);
}

FArcaneWireUUID FArcaneWireUUID::FromString(const FString &HexString) {
  // Parse 32 hex chars (16 bytes) into lo/hi
  if (HexString.Len() != 32) {
    UE_LOG(LogArcaneWireCodec, Warning, TEXT("FArcaneWireUUID::FromString: invalid hex string length %d (expected 32). String: %s"), HexString.Len(), *HexString);
    return FArcaneWireUUID();
  }

  uint64 Lo = 0, Hi = 0;
  // Lo: accumulate LSB-first to match little-endian byte order from ToString()
  for (int i = 0; i < 8; ++i) {
    FString Hex = HexString.Mid(i * 2, 2);
    uint64 Byte = (uint64)FCString::Strtoi(*Hex, nullptr, 16);
    Lo |= (Byte << (i * 8)); // byte[0] is LSB
  }
  // Hi: same pattern
  for (int i = 0; i < 8; ++i) {
    FString Hex = HexString.Mid(16 + i * 2, 2);
    uint64 Byte = (uint64)FCString::Strtoi(*Hex, nullptr, 16);
    Hi |= (Byte << (i * 8)); // byte[0] is LSB
  }
  return FArcaneWireUUID(Lo, Hi);
}

FString FArcaneWireUUID::ToString() const {
  uint8 Bytes[16];
  ToBytes(Bytes);

  FString Result;
  for (int i = 0; i < 16; ++i) {
    Result += FString::Printf(TEXT("%02x"), Bytes[i]);
  }
  return Result;
}

// Encoding functions

bool EncodeClientFrame(const FArcaneWirePlayerState &PlayerState,
                       TArray<uint8> &OutBytes) {
  flatbuffers::FlatBufferBuilder fbb(256);

  // Create user_data vector
  auto UserDataOffset = fbb.CreateVector<uint8>(PlayerState.UserData.GetData(),
                                                PlayerState.UserData.Num());

  // Create UUID structs
  ArcaneWire::UUID EntityIdFb = UuidToFlatBuffers(PlayerState.EntityId);
  ArcaneWire::Vec3Q PositionFb(PlayerState.Position.X, PlayerState.Position.Y,
                               PlayerState.Position.Z);
  ArcaneWire::Vec3Q VelocityFb(PlayerState.Velocity.X, PlayerState.Velocity.Y,
                               PlayerState.Velocity.Z);

  // Create PlayerStatePayload
  auto PayloadOffset = ArcaneWire::CreatePlayerStatePayload(
      fbb, &EntityIdFb, &PositionFb, &VelocityFb, UserDataOffset,
      PlayerState.ClientSeq);

  // Create ClientFrame with PlayerState variant
  auto FrameOffset = ArcaneWire::CreateClientFrame(
      fbb, ArcaneWire::ClientPayload_PlayerState, PayloadOffset.Union());

  fbb.Finish(FrameOffset);

  // Extract bytes
  uint8 *BufferPtr = fbb.GetBufferPointer();
  size_t BufferSize = fbb.GetSize();
  OutBytes.Reset();
  OutBytes.Append(BufferPtr, BufferSize);

  return true;
}

bool EncodeClientAction(const FArcaneWireGameAction &GameAction,
                        TArray<uint8> &OutBytes) {
  flatbuffers::FlatBufferBuilder fbb(256);

  // Create action_type string
  auto ActionTypeOffset =
      fbb.CreateString(TCHAR_TO_UTF8(*GameAction.ActionType));

  // Create payload vector
  auto PayloadOffset = fbb.CreateVector<uint8>(GameAction.Payload.GetData(),
                                               GameAction.Payload.Num());

  // Create UUID struct
  ArcaneWire::UUID EntityIdFb = UuidToFlatBuffers(GameAction.EntityId);

  // Create GameActionPayload
  auto ActionPayloadOffset = ArcaneWire::CreateGameActionPayload(
      fbb, &EntityIdFb, ActionTypeOffset, PayloadOffset);

  // Create ClientFrame with Action variant
  auto FrameOffset = ArcaneWire::CreateClientFrame(
      fbb, ArcaneWire::ClientPayload_Action, ActionPayloadOffset.Union());

  fbb.Finish(FrameOffset);

  // Extract bytes
  uint8 *BufferPtr = fbb.GetBufferPointer();
  size_t BufferSize = fbb.GetSize();
  OutBytes.Reset();
  OutBytes.Append(BufferPtr, BufferSize);

  return true;
}

// Decoding functions

bool DecodeServerFrame(const TArray<uint8> &Bytes, FArcaneWireDelta &OutDelta) {
  if (Bytes.Num() == 0) {
    return false;
  }

  flatbuffers::Verifier Verifier(Bytes.GetData(), Bytes.Num());
  if (!ArcaneWire::VerifyServerFrameBuffer(Verifier)) {
    return false;
  }

  const ArcaneWire::ServerFrame *Frame =
      ArcaneWire::GetServerFrame(Bytes.GetData());
  if (!Frame || Frame->payload_type() != ArcaneWire::ServerPayload_Delta) {
    return false;
  }

  const ArcaneWire::DeltaPayload *DeltaPayload =
      static_cast<const ArcaneWire::DeltaPayload *>(Frame->payload());
  if (!DeltaPayload) {
    return false;
  }

  // Extract basic fields
  if (DeltaPayload->source_cluster_id()) {
    OutDelta.SourceClusterId =
        UuidFromFlatBuffers(DeltaPayload->source_cluster_id());
  }
  OutDelta.Seq = DeltaPayload->seq();
  OutDelta.Tick = DeltaPayload->tick();
  OutDelta.Timestamp = DeltaPayload->timestamp();

  // Decode concatenated entity chunks
  OutDelta.Updated.Reset();
  const auto *EntityData = DeltaPayload->entity_data();
  const auto *EntityOffsets = DeltaPayload->entity_offsets();

  if (EntityData && EntityOffsets) {
    uint32 EntityCount = EntityOffsets->size();
    const uint8 *DataPtr = EntityData->data();
    uint32 DataLen = EntityData->size();

    for (uint32 i = 0; i < EntityCount; ++i) {
      uint32 StartOffset = EntityOffsets->Get(i);
      uint32 EndOffset =
          (i + 1 < EntityCount) ? EntityOffsets->Get(i + 1) : DataLen;

      if (EndOffset > DataLen || StartOffset > EndOffset) {
        return false;
      }

      const ArcaneWire::EntityState *EntityFb =
          flatbuffers::GetRoot<ArcaneWire::EntityState>(&DataPtr[StartOffset]);
      if (!EntityFb) {
        return false;
      }

      FArcaneWireEntityState Entity;
      if (EntityFb->entity_id()) {
        Entity.EntityId = UuidFromFlatBuffers(EntityFb->entity_id());
      }
      if (EntityFb->cluster_id()) {
        Entity.ClusterId = UuidFromFlatBuffers(EntityFb->cluster_id());
      }
      if (EntityFb->position()) {
        Entity.Position = FArcaneWireVec3Q(EntityFb->position()->x(),
                                           EntityFb->position()->y(),
                                           EntityFb->position()->z());
      }
      if (EntityFb->velocity()) {
        Entity.Velocity = FArcaneWireVec3Q(EntityFb->velocity()->x(),
                                           EntityFb->velocity()->y(),
                                           EntityFb->velocity()->z());
      }
      if (EntityFb->user_data()) {
        Entity.UserData.Append(EntityFb->user_data()->data(),
                               EntityFb->user_data()->size());
      }
      Entity.ClientSeq = EntityFb->client_seq();

      OutDelta.Updated.Add(Entity);
    }
  }

  // Decode removed entities
  OutDelta.Removed.Reset();
  const auto *RemovedList = DeltaPayload->removed();
  if (RemovedList) {
    for (uint32 i = 0; i < RemovedList->size(); ++i) {
      const ArcaneWire::UUID *RemovedUuid = RemovedList->Get(i);
      if (RemovedUuid) {
        OutDelta.Removed.Add(UuidFromFlatBuffers(RemovedUuid));
      }
    }
  }

  return true;
}

// Server-side encoding

bool EncodeServerDelta(const FArcaneWireDelta &Delta, TArray<uint8> &OutBytes) {
  // Pre-encode each entity as a standalone FlatBuffer
  TArray<TArray<uint8>> EntityChunks;
  TArray<uint32> EntityOffsets;

  uint32 CurrentOffset = 0;

  for (const FArcaneWireEntityState &Entity : Delta.Updated) {
    flatbuffers::FlatBufferBuilder fbb(512);

    // Create user_data vector
    auto UserDataOffset = fbb.CreateVector<uint8>(Entity.UserData.GetData(),
                                                  Entity.UserData.Num());

    // Create UUID structs
    ArcaneWire::UUID EntityIdFb = UuidToFlatBuffers(Entity.EntityId);
    ArcaneWire::UUID ClusterIdFb = UuidToFlatBuffers(Entity.ClusterId);
    ArcaneWire::Vec3Q PositionFb(Entity.Position.X, Entity.Position.Y,
                                 Entity.Position.Z);
    ArcaneWire::Vec3Q VelocityFb(Entity.Velocity.X, Entity.Velocity.Y,
                                 Entity.Velocity.Z);

    // Create EntityState
    auto EntityOffset = ArcaneWire::CreateEntityState(
        fbb, &EntityIdFb, &ClusterIdFb, &PositionFb, &VelocityFb,
        UserDataOffset, Entity.ClientSeq);

    fbb.Finish(EntityOffset);

    // Extract chunk bytes
    uint8 *BufferPtr = fbb.GetBufferPointer();
    size_t BufferSize = fbb.GetSize();
    TArray<uint8> Chunk;
    Chunk.Append(BufferPtr, BufferSize);
    EntityChunks.Add(Chunk);

    // Record offset for this entity
    EntityOffsets.Add(CurrentOffset);
    CurrentOffset += Chunk.Num();
  }

  // Concatenate all entity chunks
  TArray<uint8> ConcatenatedEntityData;
  for (const TArray<uint8> &Chunk : EntityChunks) {
    ConcatenatedEntityData.Append(Chunk);
  }

  // Create removed list
  std::vector<ArcaneWire::UUID> RemovedVec;
  for (const FArcaneWireUUID &RemovedId : Delta.Removed) {
    RemovedVec.push_back(UuidToFlatBuffers(RemovedId));
  }

  // Build the final DeltaPayload
  flatbuffers::FlatBufferBuilder fbb(1024);

  // Create vectors
  auto EntityDataOffset = fbb.CreateVector<uint8>(
      ConcatenatedEntityData.GetData(), ConcatenatedEntityData.Num());

  auto EntityOffsetsOffset =
      fbb.CreateVector<uint32>(EntityOffsets.GetData(), EntityOffsets.Num());

  auto RemovedOffset = fbb.CreateVectorOfStructs(RemovedVec);

  // Create source_cluster_id
  ArcaneWire::UUID SourceClusterIdFb = UuidToFlatBuffers(Delta.SourceClusterId);

  // Create DeltaPayload
  auto DeltaPayloadOffset = ArcaneWire::CreateDeltaPayload(
      fbb, &SourceClusterIdFb, Delta.Seq, Delta.Tick, Delta.Timestamp,
      EntityDataOffset, EntityOffsetsOffset, RemovedOffset);

  // Create ServerFrame with Delta variant
  auto ServerFrameOffset = ArcaneWire::CreateServerFrame(
      fbb, ArcaneWire::ServerPayload_Delta, DeltaPayloadOffset.Union());

  fbb.Finish(ServerFrameOffset);

  // Extract bytes
  uint8 *BufferPtr = fbb.GetBufferPointer();
  size_t BufferSize = fbb.GetSize();
  OutBytes.Reset();
  OutBytes.Append(BufferPtr, BufferSize);

  return true;
}

bool DecodeClientFrame(const TArray<uint8> &Bytes,
                       FArcaneWirePlayerState &OutPlayerState) {
  if (Bytes.Num() == 0) {
    return false;
  }

  flatbuffers::Verifier Verifier(Bytes.GetData(), Bytes.Num());
  if (!Verifier.VerifyBuffer<ArcaneWire::ClientFrame>(nullptr)) {
    return false;
  }

  const ArcaneWire::ClientFrame *Frame =
      flatbuffers::GetRoot<ArcaneWire::ClientFrame>(Bytes.GetData());
  if (!Frame ||
      Frame->payload_type() != ArcaneWire::ClientPayload_PlayerState) {
    return false;
  }

  const ArcaneWire::PlayerStatePayload *Payload =
      static_cast<const ArcaneWire::PlayerStatePayload *>(Frame->payload());
  if (!Payload) {
    return false;
  }

  if (Payload->entity_id()) {
    OutPlayerState.EntityId = UuidFromFlatBuffers(Payload->entity_id());
  }
  if (Payload->position()) {
    OutPlayerState.Position =
        FArcaneWireVec3Q(Payload->position()->x(), Payload->position()->y(),
                         Payload->position()->z());
  }
  if (Payload->velocity()) {
    OutPlayerState.Velocity =
        FArcaneWireVec3Q(Payload->velocity()->x(), Payload->velocity()->y(),
                         Payload->velocity()->z());
  }
  if (Payload->user_data()) {
    OutPlayerState.UserData.Append(Payload->user_data()->data(),
                                   Payload->user_data()->size());
  }
  OutPlayerState.ClientSeq = Payload->client_seq();

  return true;
}

bool DecodeClientAction(const TArray<uint8> &Bytes,
                        FArcaneWireGameAction &OutGameAction) {
  if (Bytes.Num() == 0) {
    return false;
  }

  flatbuffers::Verifier Verifier(Bytes.GetData(), Bytes.Num());
  if (!Verifier.VerifyBuffer<ArcaneWire::ClientFrame>(nullptr)) {
    return false;
  }

  const ArcaneWire::ClientFrame *Frame =
      flatbuffers::GetRoot<ArcaneWire::ClientFrame>(Bytes.GetData());
  if (!Frame || Frame->payload_type() != ArcaneWire::ClientPayload_Action) {
    return false;
  }

  const ArcaneWire::GameActionPayload *Payload =
      static_cast<const ArcaneWire::GameActionPayload *>(Frame->payload());
  if (!Payload) {
    return false;
  }

  if (Payload->entity_id()) {
    OutGameAction.EntityId = UuidFromFlatBuffers(Payload->entity_id());
  }
  if (Payload->action_type()) {
    OutGameAction.ActionType =
        FString(UTF8_TO_TCHAR(Payload->action_type()->c_str()));
  }
  if (Payload->payload()) {
    OutGameAction.Payload.Append(Payload->payload()->data(),
                                 Payload->payload()->size());
  }

  return true;
}

bool DecodeEntityState(const TArray<uint8> &Bytes,
                       FArcaneWireEntityState &OutEntityState) {
  if (Bytes.Num() == 0) {
    return false;
  }

  flatbuffers::Verifier Verifier(Bytes.GetData(), Bytes.Num());
  if (!Verifier.VerifyBuffer<ArcaneWire::EntityState>(nullptr)) {
    return false;
  }

  const ArcaneWire::EntityState *EntityFb =
      flatbuffers::GetRoot<ArcaneWire::EntityState>(Bytes.GetData());
  if (!EntityFb) {
    return false;
  }

  if (EntityFb->entity_id()) {
    OutEntityState.EntityId = UuidFromFlatBuffers(EntityFb->entity_id());
  }
  if (EntityFb->cluster_id()) {
    OutEntityState.ClusterId = UuidFromFlatBuffers(EntityFb->cluster_id());
  }
  if (EntityFb->position()) {
    OutEntityState.Position =
        FArcaneWireVec3Q(EntityFb->position()->x(), EntityFb->position()->y(),
                         EntityFb->position()->z());
  }
  if (EntityFb->velocity()) {
    OutEntityState.Velocity =
        FArcaneWireVec3Q(EntityFb->velocity()->x(), EntityFb->velocity()->y(),
                         EntityFb->velocity()->z());
  }
  if (EntityFb->user_data()) {
    OutEntityState.UserData.Append(EntityFb->user_data()->data(),
                                   EntityFb->user_data()->size());
  }
  OutEntityState.ClientSeq = EntityFb->client_seq();

  return true;
}
} // namespace ArcaneWireCodec
