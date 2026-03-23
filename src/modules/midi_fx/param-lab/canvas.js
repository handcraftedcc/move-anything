/*
 * Param Lab canvas overlays:
 * - Rain (MIDI-note reactive), used for inactivity visualizer.
 * - Rotating slit-disc, used for `type: "canvas"` parameter preview.
 */

const state = {
    drops: [],
    active: {},
    seq: 0,
    lastDspSeq: 0
};

function resetState() {
    state.drops = [];
    state.active = {};
    state.seq = 0;
    state.lastDspSeq = 0;
}

function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
}

function dropLengthFromHold(holdMs, velocity) {
    const held = Math.max(0, Number(holdMs) || 0);
    const vel = clamp(Number(velocity) || 0, 0, 127);
    const fromHold = Math.round(4 + held / 40);
    const fromVelocity = Math.round(vel / 28);
    return clamp(fromHold + fromVelocity, 3, 48);
}

function parseNoteEvent(data) {
    if (!data || data.length < 3) return null;

    // Standard [status, note, velocity]
    let status = data[0] & 0xFF;
    let note = data[1] & 0x7F;
    let value = data[2] & 0x7F;
    let statusNibble = status & 0xF0;

    // USB-MIDI style [cin/cable, status, note, velocity]
    if (statusNibble !== 0x90 && statusNibble !== 0x80 && data.length >= 4) {
        const maybeStatus = data[1] & 0xFF;
        const maybeNibble = maybeStatus & 0xF0;
        if (maybeNibble === 0x90 || maybeNibble === 0x80) {
            status = maybeStatus;
            note = data[2] & 0x7F;
            value = data[3] & 0x7F;
            statusNibble = maybeNibble;
        }
    }

    // CIN-style packet (some paths provide [cin/cable, note, velocity])
    // e.g. [0x09, note, vel] or [0x29, note, vel]
    if (statusNibble !== 0x90 && statusNibble !== 0x80) {
        const cin = status & 0x0F;
        if (cin === 0x09 || cin === 0x08) {
            statusNibble = cin === 0x09 ? 0x90 : 0x80;
            note = data[1] & 0x7F;
            value = data[2] & 0x7F;
            status = statusNibble;
        } else {
            return null;
        }
    }

    return {
        statusNibble,
        channel: status & 0x0F,
        note,
        value
    };
}

function addDrop(ctx, len, xOverride) {
    const x = (typeof xOverride === "number")
        ? clamp(Math.round(xOverride), 0, ctx.width - 1)
        : Math.floor(ctx.random() * ctx.width);

    const drop = {
        id: ++state.seq,
        x,
        y: -1,
        speed: 1 + Math.floor(ctx.random() * 3),
        len: Math.max(2, Math.round(len)),
        released: true,
        noteKey: ""
    };
    state.drops.push(drop);

    if (state.drops.length > 128) {
        const extra = state.drops.length - 128;
        const removed = state.drops.splice(0, extra);
        for (const d of removed) {
            if (!d || !d.noteKey) continue;
            const active = state.active[d.noteKey];
            if (active && active.dropId === d.id) {
                delete state.active[d.noteKey];
            }
        }
    }
    return drop;
}

function handleNoteEvent(ctx, source, msg) {
    // Ignore Move knob touch note events in internal stream.
    if (source === "internal" && msg.note < 10) return;

    const now = ctx.now();
    const key = `${msg.channel}:${msg.note}`;
    const isNoteOn = msg.statusNibble === 0x90 && msg.value > 0;

    if (isNoteOn) {
        const existing = state.active[key];
        if (existing) {
            const existingDrop = state.drops.find(d => d && d.id === existing.dropId);
            if (existingDrop) existingDrop.released = true;
        }

        const drop = addDrop(ctx, dropLengthFromHold(0, msg.value));
        drop.released = false;
        drop.noteKey = key;
        state.active[key] = {
            startedAt: now,
            velocity: msg.value,
            dropId: drop.id
        };
        return;
    }

    const active = state.active[key];
    if (active) {
        const drop = state.drops.find(d => d && d.id === active.dropId);
        if (drop) {
            const holdMs = Math.max(0, now - active.startedAt);
            drop.len = dropLengthFromHold(holdMs, active.velocity);
            drop.released = true;
        }
        delete state.active[key];
    } else {
        addDrop(ctx, dropLengthFromHold(0, msg.value));
    }
}

function onMidi(ctx, event) {
    const data = event && Array.isArray(event.data) ? event.data : [];
    const source = event && event.source ? String(event.source) : "";
    const msg = parseNoteEvent(data);
    if (!msg) return;
    handleNoteEvent(ctx, source, msg);
}

function pollDspMidi(ctx) {
    if (!ctx || typeof ctx.getParam !== "function") return;
    const raw = ctx.getParam("canvas_midi");
    if (!raw || typeof raw !== "string") return;

    // Format: "seq,status,note,value"
    const parts = raw.split(",");
    if (parts.length < 4) return;

    const seq = parseInt(parts[0], 10);
    const status = parseInt(parts[1], 10);
    const note = parseInt(parts[2], 10);
    const value = parseInt(parts[3], 10);
    if (!Number.isFinite(seq) || seq <= state.lastDspSeq) return;
    if (!Number.isFinite(status) || !Number.isFinite(note) || !Number.isFinite(value)) return;

    state.lastDspSeq = seq;

    const statusNibble = status & 0xF0;
    if (statusNibble !== 0x90 && statusNibble !== 0x80) return;
    handleNoteEvent(ctx, "dsp", {
        statusNibble,
        channel: status & 0x0F,
        note: note & 0x7F,
        value: value & 0x7F
    });
}

function tick(ctx) {
    pollDspMidi(ctx);

    const now = ctx.now();
    for (let i = state.drops.length - 1; i >= 0; i--) {
        const drop = state.drops[i];
        if (!drop) continue;

        if (!drop.released && drop.noteKey) {
            const active = state.active[drop.noteKey];
            if (active) {
                drop.len = dropLengthFromHold(Math.max(0, now - active.startedAt), active.velocity);
            }
        }

        drop.y += drop.speed;
        if (drop.y - drop.len > ctx.height) {
            if (drop.noteKey) {
                const active = state.active[drop.noteKey];
                if (active && active.dropId === drop.id) delete state.active[drop.noteKey];
            }
            state.drops.splice(i, 1);
        }
    }
}

function draw(ctx) {
    ctx.clear();

    for (const drop of state.drops) {
        if (!drop) continue;
        for (let i = 0; i < drop.len; i++) {
            const y = drop.y - i;
            if (y >= 0 && y < ctx.height) {
                ctx.setPixel(drop.x, y, 1);
            }
        }
    }

}

function onOpen(_ctx, _event) {
    resetState();
}

function onClose(_ctx, _event) {
    resetState();
}

function createRainOverlay() {
    return {
        onOpen,
        onClose,
        onMidi,
        tick,
        draw
    };
}

const rainOverlay = createRainOverlay();

const rotorState = {
    angleRad: 0
};

const JOG_CC = 14;
const TWO_PI = Math.PI * 2;

function normalizeAngle(rad) {
    let out = Number(rad) || 0;
    while (out > Math.PI) out -= TWO_PI;
    while (out < -Math.PI) out += TWO_PI;
    return out;
}

function decodeRelativeDelta(value) {
    const v = Number(value) || 0;
    if (v === 0 || v === 64) return 0;
    if (v < 64) return v;
    return v - 128;
}

function parseCCEvent(data) {
    if (!data || data.length < 3) return null;

    // Standard [status, cc, value]
    let status = data[0] & 0xFF;
    let cc = data[1] & 0x7F;
    let value = data[2] & 0x7F;
    let statusNibble = status & 0xF0;

    // USB-MIDI style [cin/cable, status, cc, value]
    if (statusNibble !== 0xB0 && data.length >= 4) {
        const maybeStatus = data[1] & 0xFF;
        const maybeNibble = maybeStatus & 0xF0;
        if (maybeNibble === 0xB0) {
            status = maybeStatus;
            cc = data[2] & 0x7F;
            value = data[3] & 0x7F;
            statusNibble = maybeNibble;
        }
    }

    // CIN-style packet [cin/cable, cc, value]
    if (statusNibble !== 0xB0) {
        const cin = status & 0x0F;
        if (cin === 0x0B) {
            status = 0xB0;
            statusNibble = 0xB0;
            cc = data[1] & 0x7F;
            value = data[2] & 0x7F;
        } else {
            return null;
        }
    }

    return {
        statusNibble,
        channel: status & 0x0F,
        cc,
        value
    };
}

function rotorOnOpen(_ctx, _event) {
    rotorState.angleRad = 0;
}

function rotorOnClose(_ctx, _event) {
    rotorState.angleRad = 0;
}

function rotorOnMidi(_ctx, event) {
    const data = event && Array.isArray(event.data) ? event.data : [];
    const msg = parseCCEvent(data);
    if (!msg || msg.statusNibble !== 0xB0 || msg.cc !== JOG_CC) return;

    const delta = decodeRelativeDelta(msg.value);
    if (!delta) return;

    // 6 degrees per click gives visible but controllable rotation.
    const stepRad = Math.PI / 30;
    rotorState.angleRad = normalizeAngle(rotorState.angleRad + delta * stepRad);
}

function drawFilledCircle(ctx, cx, cy, r, value) {
    for (let dy = -r; dy <= r; dy++) {
        const span = Math.floor(Math.sqrt(Math.max(0, r * r - dy * dy)));
        const y = cy + dy;
        if (y < 0 || y >= ctx.height) continue;
        for (let dx = -span; dx <= span; dx++) {
            const x = cx + dx;
            if (x < 0 || x >= ctx.width) continue;
            ctx.setPixel(x, y, value);
        }
    }
}

function drawSlit(ctx, cx, cy, angleRad, halfLen, halfWidth) {
    const ux = Math.cos(angleRad);
    const uy = Math.sin(angleRad);
    for (let t = -halfLen; t <= halfLen; t++) {
        const bx = cx + ux * t;
        const by = cy + uy * t;
        for (let w = -halfWidth; w <= halfWidth; w++) {
            const px = Math.round(bx - uy * w);
            const py = Math.round(by + ux * w);
            if (px < 0 || px >= ctx.width || py < 0 || py >= ctx.height) continue;
            ctx.setPixel(px, py, 0);
        }
    }
}

function rotorDraw(ctx) {
    ctx.clear();

    const cx = Math.floor(ctx.width / 2);
    const cy = Math.floor(ctx.height / 2);
    const radius = Math.max(10, Math.min(cx, cy) - 3);

    drawFilledCircle(ctx, cx, cy, radius, 1);
    drawSlit(ctx, cx, cy, rotorState.angleRad, Math.max(4, radius - 2), 2);
}

function createRotatingDiscOverlay() {
    return {
        onOpen: rotorOnOpen,
        onClose: rotorOnClose,
        onMidi: rotorOnMidi,
        draw: rotorDraw
    };
}

const rotorOverlay = createRotatingDiscOverlay();

// Backward-compatible default export
globalThis.canvas_overlay = rainOverlay;

// Named overlays: can be targeted with "canvas.js#rain"
if (!globalThis.canvas_overlays || typeof globalThis.canvas_overlays !== "object") {
    globalThis.canvas_overlays = {};
}
globalThis.canvas_overlays.rain = rainOverlay;
globalThis.canvas_overlays.rotor = rotorOverlay;

// Factory target: can be targeted with "canvas.js#createRainOverlay"
globalThis.createRainOverlay = createRainOverlay;
globalThis.createRotatingDiscOverlay = createRotatingDiscOverlay;
