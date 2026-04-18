import type { ChatMessageDirection, ChatRecord } from "@/lib/chat-types";

const CHAT_DECODER = new TextDecoder();
const CHAT_ENCODER = new TextEncoder();

export const FRAME_HEADER_BYTES = 30;
const MAX_PROTOBUF_VARINT_BYTES = 10;
const BIGINT_ZERO = BigInt(0);
const BIGINT_SEVEN = BigInt(7);
const BIGINT_MASK_7F = BigInt("0x7f");

export const enum WireMessageType {
  CHAT = 0x01,
  AUTH = 0x02,
  PING = 0x03,
  PONG = 0x04,
  ERROR = 0x05,
}

export type ParsedWireFrame = {
  msgType: WireMessageType;
  msgId: string;
  senderId: string;
  recipientId: string;
  payload: Uint8Array;
};

type VarintResult = {
  value: bigint;
  nextOffset: number;
};

function toBigInt(value: bigint | number | string | undefined) {
  if (value === undefined) {
    return BIGINT_ZERO;
  }

  if (typeof value === "bigint") {
    return value;
  }

  if (typeof value === "number") {
    return BigInt(value);
  }

  return BigInt(value);
}

function concatBytes(first: Uint8Array, second: Uint8Array) {
  const merged = new Uint8Array(first.length + second.length);
  merged.set(first);
  merged.set(second, first.length);
  return merged;
}

function encodeVarint(value: bigint) {
  const bytes: number[] = [];
  let remaining = value;

  do {
    let byte = Number(remaining & BIGINT_MASK_7F);
    remaining >>= BIGINT_SEVEN;

    if (remaining > BIGINT_ZERO) {
      byte |= 0x80;
    }

    bytes.push(byte);
  } while (remaining > BIGINT_ZERO);

  return Uint8Array.from(bytes);
}

function readVarint(bytes: Uint8Array, startOffset: number): VarintResult {
  let value = BIGINT_ZERO;
  let shift = BIGINT_ZERO;
  let offset = startOffset;

  for (let index = 0; index < MAX_PROTOBUF_VARINT_BYTES; index += 1) {
    if (offset >= bytes.length) {
      throw new Error("Unexpected end of protobuf payload");
    }

    const byte = bytes[offset];
    value |= BigInt(byte & 0x7f) << shift;
    offset += 1;

    if ((byte & 0x80) === 0) {
      return {
        value,
        nextOffset: offset,
      };
    }

    shift += BIGINT_SEVEN;
  }

  throw new Error("Invalid protobuf varint");
}

function skipUnknownField(
  bytes: Uint8Array,
  wireType: number,
  offset: number,
) {
  if (wireType === 0) {
    return readVarint(bytes, offset).nextOffset;
  }

  if (wireType === 2) {
    const length = readVarint(bytes, offset);
    return length.nextOffset + Number(length.value);
  }

  throw new Error(`Unsupported protobuf wire type: ${wireType}`);
}

function pushField(target: number[], fieldNumber: number, wireType: number) {
  target.push(...encodeVarint(BigInt((fieldNumber << 3) | wireType)));
}

export function utf8Encode(value: string) {
  return CHAT_ENCODER.encode(value);
}

export function encodeFrame(params: {
  msgType: WireMessageType;
  payload?: Uint8Array;
  msgId?: bigint | number | string;
  senderId?: bigint | number | string;
  recipientId?: bigint | number | string;
}) {
  const payload = params.payload ?? new Uint8Array(0);
  const frame = new Uint8Array(FRAME_HEADER_BYTES + payload.length);
  const view = new DataView(frame.buffer);

  view.setUint32(0, payload.length, true);
  view.setUint8(4, params.msgType);
  view.setUint8(5, 0);
  view.setBigUint64(6, toBigInt(params.msgId), true);
  view.setBigUint64(14, toBigInt(params.senderId), true);
  view.setBigUint64(22, toBigInt(params.recipientId), true);

  frame.set(payload, FRAME_HEADER_BYTES);
  return frame;
}

export function extractFrames(buffer: Uint8Array) {
  let offset = 0;
  const frames: ParsedWireFrame[] = [];

  while (buffer.length - offset >= FRAME_HEADER_BYTES) {
    const view = new DataView(
      buffer.buffer,
      buffer.byteOffset + offset,
      FRAME_HEADER_BYTES,
    );
    const payloadLength = view.getUint32(0, true);
    const frameLength = FRAME_HEADER_BYTES + payloadLength;

    if (buffer.length - offset < frameLength) {
      break;
    }

    const payloadStart = offset + FRAME_HEADER_BYTES;
    const payloadEnd = payloadStart + payloadLength;

    frames.push({
      msgType: view.getUint8(4) as WireMessageType,
      msgId: view.getBigUint64(6, true).toString(),
      senderId: view.getBigUint64(14, true).toString(),
      recipientId: view.getBigUint64(22, true).toString(),
      payload: buffer.slice(payloadStart, payloadEnd),
    });

    offset += frameLength;
  }

  return {
    frames,
    remainder: buffer.slice(offset),
  };
}

export function appendFrameChunk(current: Uint8Array, chunk: Uint8Array) {
  return concatBytes(current, chunk);
}

export function decodeChatMessage(
  payload: Uint8Array,
  direction: ChatMessageDirection,
): ChatRecord | null {
  let offset = 0;
  let msgId = "";
  let senderId = "";
  let recipientId = "";
  let content = "";
  let timestampMs = Date.now();

  try {
    while (offset < payload.length) {
      const tag = readVarint(payload, offset);
      const fieldNumber = Number(tag.value >> BigInt(3));
      const wireType = Number(tag.value & BIGINT_SEVEN);
      offset = tag.nextOffset;

      switch (fieldNumber) {
        case 1: {
          const value = readVarint(payload, offset);
          msgId = value.value.toString();
          offset = value.nextOffset;
          break;
        }
        case 2: {
          const value = readVarint(payload, offset);
          senderId = value.value.toString();
          offset = value.nextOffset;
          break;
        }
        case 3: {
          const value = readVarint(payload, offset);
          recipientId = value.value.toString();
          offset = value.nextOffset;
          break;
        }
        case 4: {
          const length = readVarint(payload, offset);
          const byteLength = Number(length.value);
          const start = length.nextOffset;
          const end = start + byteLength;
          content = CHAT_DECODER.decode(payload.slice(start, end));
          offset = end;
          break;
        }
        case 5: {
          const value = readVarint(payload, offset);
          timestampMs = Number(value.value);
          offset = value.nextOffset;
          break;
        }
        case 6: {
          const value = readVarint(payload, offset);
          offset = value.nextOffset;

          if (value.value !== BIGINT_ZERO) {
            return null;
          }

          break;
        }
        default: {
          offset = skipUnknownField(payload, wireType, offset);
          break;
        }
      }
    }
  } catch {
    return null;
  }

  if (!senderId || !recipientId) {
    return null;
  }

  return {
    id: msgId || `proto-${timestampMs}-${senderId}-${recipientId}`,
    senderId,
    recipientId,
    content,
    timestampMs,
    direction,
  };
}

export function encodeChatMessageContent(content: string) {
  return utf8Encode(content);
}

export function encodeChatMessageProto(message: {
  id: string;
  senderId: string;
  recipientId: string;
  content: string;
  timestampMs: number;
}) {
  const bytes: number[] = [];

  pushField(bytes, 1, 0);
  bytes.push(...encodeVarint(BigInt(message.id)));

  pushField(bytes, 2, 0);
  bytes.push(...encodeVarint(BigInt(message.senderId)));

  pushField(bytes, 3, 0);
  bytes.push(...encodeVarint(BigInt(message.recipientId)));

  const contentBytes = utf8Encode(message.content);
  pushField(bytes, 4, 2);
  bytes.push(...encodeVarint(BigInt(contentBytes.length)));
  bytes.push(...contentBytes);

  pushField(bytes, 5, 0);
  bytes.push(...encodeVarint(BigInt(message.timestampMs)));

  pushField(bytes, 6, 0);
  bytes.push(...encodeVarint(BIGINT_ZERO));

  return Uint8Array.from(bytes);
}
