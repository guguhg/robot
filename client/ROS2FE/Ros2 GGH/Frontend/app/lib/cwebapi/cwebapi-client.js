/* C.WebApi browser SDK (ES module)
 * Runtime dependencies for realtime features:
 *   window.signalR from @microsoft/signalr 8.0.7
 *   window.signalR.protocols.msgpack from @microsoft/signalr-protocol-msgpack 8.0.7
 */

export class CWebApiError extends Error {
    constructor(message, { status = 0, code = "", details = null, response = null, correlationId = "" } = {}) {
        super(message || `C.WebApi request failed (${status || "network"})`);
        this.name = "CWebApiError";
        this.status = status;
        this.code = code;
        this.details = details;
        this.response = response;
        this.correlationId = correlationId;
    }
}

export class CWebApiClient {
    constructor({
        baseUrl = "",
        accessToken = "",
        tokenProvider = null,
        fetchImpl = globalThis.fetch,
        onUnauthorized = null
    } = {}) {
        if (typeof fetchImpl !== "function") throw new TypeError("fetchImpl must be a function");
        this.baseUrl = String(baseUrl || "").replace(/\/$/, "");
        this._accessToken = accessToken || "";
        this._tokenProvider = tokenProvider;
        this._fetch = fetchImpl.bind(globalThis);
        this._onUnauthorized = onUnauthorized;
    }

    setAccessToken(token) { this._accessToken = token || ""; }
    clearAccessToken() { this._accessToken = ""; }
    getAccessToken() {
        const supplied = typeof this._tokenProvider === "function" ? this._tokenProvider() : "";
        return supplied || this._accessToken || "";
    }

    async request(path, { method = "GET", body, headers = {}, auth = true, signal } = {}) {
        const requestHeaders = { ...headers };
        if (body !== undefined && body !== null && !hasHeader(requestHeaders, "Content-Type")) {
            requestHeaders["Content-Type"] = "application/json";
        }
        const token = auth ? this.getAccessToken() : "";
        if (token && !hasHeader(requestHeaders, "Authorization")) {
            requestHeaders.Authorization = `Bearer ${token}`;
        }

        let response;
        try {
            response = await this._fetch(this.url(path), {
                method,
                headers: requestHeaders,
                body: body === undefined || body === null
                    ? undefined
                    : (typeof body === "string" || body instanceof FormData ? body : JSON.stringify(body)),
                signal
            });
        } catch (error) {
            throw new CWebApiError(error?.message || "网络请求失败", { details: error });
        }

        if (!response.ok) {
            const error = await toApiError(response);
            if (response.status === 401 && typeof this._onUnauthorized === "function") {
                await this._onUnauthorized(error);
            }
            throw error;
        }
        if (response.status === 204 || response.status === 205) return undefined;
        const contentType = response.headers.get("content-type") || "";
        if (contentType.includes("application/json") || contentType.includes("problem+json")) return response.json();
        return response.text();
    }

    url(path) {
        if (/^https?:\/\//i.test(path)) return path;
        const normalized = path.startsWith("/") ? path : `/${path}`;
        return `${this.baseUrl}${normalized}`;
    }

    login(identifier, password, options) {
        return this.request("/api/auth/login", {
            method: "POST", body: { username: identifier, password }, auth: false, ...options
        });
    }
    guestLogin(options) {
        return this.request("/api/auth/guest", { method: "POST", auth: false, ...options });
    }
    register(username, email, password, options) {
        return this.request("/api/auth/register", {
            method: "POST", body: { username, email, password }, auth: false, ...options
        });
    }
    verifyEmail(email, code, options) {
        return this.request("/api/auth/verify-email", { method: "POST", body: { email, code }, auth: false, ...options });
    }
    resendVerification(email, options) {
        return this.request("/api/auth/resend-verification", { method: "POST", body: { email }, auth: false, ...options });
    }
    forgotPassword(email, options) {
        return this.request("/api/auth/forgot-password", { method: "POST", body: { email }, auth: false, ...options });
    }
    resetPassword(email, code, newPassword, options) {
        return this.request("/api/auth/reset-password", {
            method: "POST", body: { email, code, newPassword }, auth: false, ...options
        });
    }

    getHealth(options) { return this.request("/health", { auth: false, ...options }); }
    getReady(options) { return this.request("/ready", { auth: false, ...options }); }
    getDevices(options) { return this.request("/api/devices", options); }
    getDevice(deviceId, options) { return this.request(`/api/devices/${enc(deviceId)}`, options); }
    getStreamUrl(deviceId, options) {
        return this.request(`/api/devices/${enc(deviceId)}/stream-url`, options);
    }
    getPublishUrl(deviceId, options) {
        return this.request(`/api/devices/${enc(deviceId)}/publish-url`, options);
    }
    getPointCloudStreams(options) { return this.request("/api/pointcloud/streams", options); }

    getCurrentOperator(options) { return this.request("/api/operator/current", options); }
    getOperatorUsers(options) { return this.request("/api/operator/users", options); }
    assignOperator(userId, options) {
        return this.request("/api/operator/assign", {
            method: "POST", body: { userId }, ...options
        });
    }
    reclaimOperator(options) {
        return this.request("/api/operator/reclaim", { method: "POST", ...options });
    }

    buildHub(path, { messagePack = false, automaticReconnect = true } = {}) {
        const signalR = requireSignalR(messagePack);
        let builder = new signalR.HubConnectionBuilder().withUrl(this.url(path), {
            accessTokenFactory: () => this.getAccessToken()
        });
        if (automaticReconnect) builder = builder.withAutomaticReconnect();
        if (messagePack) {
            builder = builder.withHubProtocol(new signalR.protocols.msgpack.MessagePackHubProtocol());
        }
        return builder.build();
    }

    createPointCloudHub(options) { return new PointCloudHubClient(this, options); }
    createMapHub(handlers, options) { return new MapHubClient(this, handlers, options); }
    createRobotControl(options) { return new RobotControlClient(this, options); }
    createChatHub(options) { return new ChatHubClient(this, options); }
    createOperatorClient(options) { return new OperatorClient(this, options); }
    createWhepPlayer(videoElement, options) { return new WhepPlayer(this, videoElement, options); }
    createWhipPublisher(options) { return new WhipPublisher(this, options); }
}

export class PointCloudHubClient {
    constructor(api, { automaticReconnect = true } = {}) {
        this.connection = api.buildHub("/hubs/pointcloud", { messagePack: true, automaticReconnect });
        this._frameHandlers = new Set();
        this._subscriptions = new Set();
        this.connection.on("PointCloud", (stream, payload) => {
            const frame = decodePointCloudFrame(payload);
            for (const handler of this._frameHandlers) handler(stream, frame, payload);
        });
        this.connection.onreconnected(async () => {
            for (const stream of this._subscriptions) {
                try { await this.connection.invoke("Subscribe", stream); }
                catch { /* connection may close again while restoring */ }
            }
        });
    }
    onFrame(handler) { this._frameHandlers.add(handler); return () => this._frameHandlers.delete(handler); }
    start() { return this.connection.start(); }
    stop() { return this.connection.stop(); }
    async subscribe(stream) {
        const name = String(stream || "").trim();
        if (!name) throw new TypeError("stream is required");
        await this.connection.invoke("Subscribe", name);
        this._subscriptions.add(name);
    }
    async unsubscribe(stream) {
        const name = String(stream || "").trim();
        if (!name) return;
        await this.connection.invoke("Unsubscribe", name);
        this._subscriptions.delete(name);
    }
}

export class MapHubClient {
    constructor(api, handlers = {}, { automaticReconnect = true } = {}) {
        this.connection = api.buildHub("/hubs/map", { messagePack: true, automaticReconnect });
        this._subscribed = false;
        this.setHandlers(handlers);
        this.connection.onreconnected(async () => {
            if (this._subscribed) {
                try { await this.connection.invoke("Subscribe"); }
                catch { /* connection may close again while restoring */ }
            }
        });
    }
    setHandlers(h = {}) {
        const bind = (event, decoder, key) => this.connection.on(event, payload => {
            let value;
            try { value = decoder(payload); }
            catch (error) {
                if (h.onFrameError) { h.onFrameError(event, error, payload); return; }
                throw error;
            }
            h[key]?.(value, payload);
        });
        bind("Map", decodeMapFrame, "onMap");
        bind("RobotPose", decodeRobotPose, "onRobotPose");
        bind("Particles", decodePointPairs, "onParticles");
        bind("Path", decodePointPairs, "onPath");
        bind("LocalPlan", decodePointPairs, "onLocalPlan");
        bind("Scan", decodePointPairs, "onScan");
        bind("GlobalCostmap", decodeCostmap, "onGlobalCostmap");
        bind("LocalCostmap", decodeCostmap, "onLocalCostmap");
        bind("MapPatch", decodeCostmap, "onMapPatch");
        bind("Odometry", decodeOdometry, "onOdometry");
        bind("CameraImage", decodeCameraImage, "onCameraImage");
        this.connection.on("BatterySoc", value => h.onBatterySoc?.(Number(value)));
        this.connection.on("BatteryVoltage", value => h.onBatteryVoltage?.(Number(value)));
        this.connection.on("SafeTwist", (linear, angular) => h.onSafeTwist?.(Number(linear), Number(angular)));
        return this;
    }
    start() { return this.connection.start(); }
    stop() { return this.connection.stop(); }
    async subscribe() {
        await this.connection.invoke("Subscribe");
        this._subscribed = true;
    }
    setGoal(x, y, theta) { return this.connection.invoke("SetGoal", finite(x), finite(y), finite(theta)); }
    setInitialPose(x, y, theta) { return this.connection.invoke("SetInitialPose", finite(x), finite(y), finite(theta)); }
}

export class RobotControlClient {
    constructor(api, { hz = 15, onError = null } = {}) {
        this.connection = api.buildHub("/hubs/command");
        this.hz = hz;
        this.onError = onError;
        this._timer = null;
        this._provider = null;
        this._sending = false;
    }
    start() { return this.connection.start(); }
    async stop() { await this.stopVelocityLoop(true); await this.connection.stop(); }
    setVelocity(linearX, linearY, angularZ) {
        return this.connection.invoke("SetVelocity", finite(linearX), finite(linearY), finite(angularZ));
    }
    stopRobot() { return this.connection.invoke("StopRobot"); }
    beginVelocity(provider, hz = this.hz) {
        this.stopVelocityLoop(false);
        if (typeof provider !== "function") throw new TypeError("velocity provider must be a function");
        this._provider = provider;
        const interval = Math.max(20, 1000 / Math.max(1, hz));
        this._timer = setInterval(() => this._tick(), interval);
        this._tick();
    }
    async stopVelocityLoop(sendStop = true) {
        if (this._timer !== null) clearInterval(this._timer);
        this._timer = null;
        this._provider = null;
        if (sendStop && this.connection.state === "Connected") await this.stopRobot();
    }
    async dispose() { await this.stop(); }
    async _tick() {
        if (this._sending || !this._provider || this.connection.state !== "Connected") return;
        let value;
        try { value = this._provider(); }
        catch (error) { this.onError?.(error); return; }
        this._sending = true;
        try {
            await this.setVelocity(value.linearX ?? 0, value.linearY ?? 0, value.angularZ ?? 0);
        } catch (error) {
            this.onError?.(error);
        } finally {
            this._sending = false;
        }
    }
}

export class ChatHubClient {
    constructor(api, { automaticReconnect = true } = {}) {
        this.connection = api.buildHub("/hubs/chat", { automaticReconnect });
        this._messageHandlers = new Set();
        this._historyHandlers = new Set();
        this._operatorHandlers = new Set();
        this.connection.on("ReceiveMessage", (author, text, ts) => {
            const message = { author: String(author || ""), text: String(text || ""), ts: String(ts || "") };
            for (const handler of this._messageHandlers) handler(message);
        });
        this.connection.on("History", messages => {
            const value = Array.isArray(messages) ? messages : [];
            for (const handler of this._historyHandlers) handler(value);
        });
        this.connection.on("OperatorChanged", value => {
            const change = {
                userId: value?.userId ?? null,
                username: value?.username ?? null
            };
            for (const handler of this._operatorHandlers) handler(change);
        });
    }
    onMessage(handler) { this._messageHandlers.add(handler); return () => this._messageHandlers.delete(handler); }
    onHistory(handler) { this._historyHandlers.add(handler); return () => this._historyHandlers.delete(handler); }
    onOperatorChanged(handler) { this._operatorHandlers.add(handler); return () => this._operatorHandlers.delete(handler); }
    start() { return this.connection.start(); }
    stop() { return this.connection.stop(); }
    sendMessage(text) { return this.connection.invoke("SendMessage", String(text ?? "")); }
}

export class OperatorClient {
    constructor(api, { automaticReconnect = true } = {}) {
        this.api = api;
        this.connection = api.buildHub("/hubs/chat", { automaticReconnect });
        this._handlers = new Set();
        this.connection.on("OperatorChanged", async () => {
            try {
                const state = await this.getCurrent();
                for (const handler of this._handlers) handler(state);
            } catch { /* REST auth may have expired; caller handles 401 */ }
        });
    }
    onChanged(handler) { this._handlers.add(handler); return () => this._handlers.delete(handler); }
    start() { return this.connection.start(); }
    stop() { return this.connection.stop(); }
    getCurrent(options) { return this.api.getCurrentOperator(options); }
    listUsers(options) { return this.api.getOperatorUsers(options); }
    assign(userId, options) { return this.api.assignOperator(userId, options); }
    reclaim(options) { return this.api.reclaimOperator(options); }
}

export class WhepPlayer {
    constructor(api, videoElement, { rtcConfiguration, iceGatheringTimeoutMs = 2000, onStateChange = null } = {}) {
        if (!videoElement) throw new TypeError("videoElement is required");
        this.api = api;
        this.video = videoElement;
        this.rtcConfiguration = rtcConfiguration;
        this.iceGatheringTimeoutMs = iceGatheringTimeoutMs;
        this.onStateChange = onStateChange;
        this.peerConnection = null;
    }
    async start(deviceId) {
        const info = await this.api.getStreamUrl(deviceId);
        this.stop();
        const pc = new RTCPeerConnection(this.rtcConfiguration);
        this.peerConnection = pc;
        pc.addTransceiver("video", { direction: "recvonly" });
        pc.addTransceiver("audio", { direction: "recvonly" });
        pc.ontrack = event => {
            this.video.srcObject = event.streams[0] || new MediaStream([event.track]);
            this.video.play?.().catch(() => {});
        };
        pc.onconnectionstatechange = () => this.onStateChange?.(pc.connectionState);

        try {
            const offer = await pc.createOffer();
            await pc.setLocalDescription(offer);
            await waitForIceGathering(pc, this.iceGatheringTimeoutMs);
            const response = await this.api._fetch(info.whepUrl, {
                method: "POST",
                headers: { "Content-Type": "application/sdp" },
                body: pc.localDescription?.sdp || offer.sdp
            });
            if (!response.ok) throw await toApiError(response);
            await pc.setRemoteDescription({ type: "answer", sdp: await response.text() });
            return info;
        } catch (error) {
            this.stop();
            throw error;
        }
    }
    stop() {
        if (this.peerConnection) this.peerConnection.close();
        this.peerConnection = null;
        this.video.srcObject = null;
    }
}

export class WhipPublisher {
    constructor(api, { rtcConfiguration, iceGatheringTimeoutMs = 2000, onStateChange = null } = {}) {
        this.api = api;
        this.rtcConfiguration = rtcConfiguration;
        this.iceGatheringTimeoutMs = iceGatheringTimeoutMs;
        this.onStateChange = onStateChange;
        this.peerConnection = null;
        this.mediaStream = null;
        this._ownsTracks = false;
    }
    async start(deviceId, streamOrConstraints = { video: true, audio: true }) {
        const info = await this.api.getPublishUrl(deviceId);
        this.stop();
        let stream;
        if (isMediaStream(streamOrConstraints)) {
            stream = streamOrConstraints;
            this._ownsTracks = false;
        } else {
            if (!globalThis.navigator?.mediaDevices?.getUserMedia) {
                throw new Error("getUserMedia is not available; pass an existing MediaStream instead.");
            }
            stream = await globalThis.navigator.mediaDevices.getUserMedia(streamOrConstraints);
            this._ownsTracks = true;
        }
        this.mediaStream = stream;

        const pc = new RTCPeerConnection(this.rtcConfiguration);
        this.peerConnection = pc;
        for (const track of stream.getTracks()) pc.addTrack(track, stream);
        pc.onconnectionstatechange = () => this.onStateChange?.(pc.connectionState);

        try {
            const offer = await pc.createOffer();
            await pc.setLocalDescription(offer);
            await waitForIceGathering(pc, this.iceGatheringTimeoutMs);
            const response = await this.api._fetch(info.whipUrl, {
                method: "POST",
                headers: { "Content-Type": "application/sdp" },
                body: pc.localDescription?.sdp || offer.sdp
            });
            if (!response.ok) throw await toApiError(response);
            await pc.setRemoteDescription({ type: "answer", sdp: await response.text() });
            return info;
        } catch (error) {
            this.stop();
            throw error;
        }
    }
    stop() {
        if (this.peerConnection) this.peerConnection.close();
        this.peerConnection = null;
        if (this._ownsTracks && this.mediaStream) {
            for (const track of this.mediaStream.getTracks()) track.stop();
        }
        this.mediaStream = null;
        this._ownsTracks = false;
    }
}

export function decodePointCloudFrame(value) {
    const bytes = asBytes(value);
    requireLength(bytes, 4, "point cloud");
    const view = dataView(bytes);
    const count = view.getUint32(0, true);
    requireLength(bytes, 4 + count * 12, "point cloud");
    const xyz = new Float32Array(count * 3);
    for (let i = 0; i < xyz.length; i++) xyz[i] = view.getFloat32(4 + i * 4, true);
    return { count, xyz };
}

export function decodeMapFrame(value) {
    const bytes = asBytes(value);
    requireLength(bytes, 25, "map");
    const view = dataView(bytes);
    if (view.getUint8(0) !== 0x4d) throw new RangeError("Invalid map magic byte");
    const width = view.getUint32(1, true);
    const height = view.getUint32(5, true);
    requireLength(bytes, 25 + width * height, "map");
    return {
        width,
        height,
        resolution: view.getFloat32(9, true),
        originX: view.getFloat32(13, true),
        originY: view.getFloat32(17, true),
        originYaw: view.getFloat32(21, true),
        data: new Int8Array(bytes.buffer, bytes.byteOffset + 25, width * height)
    };
}

export function decodeRobotPose(value) {
    const bytes = asBytes(value); requireLength(bytes, 12, "robot pose");
    const view = dataView(bytes);
    return { x: view.getFloat32(0, true), y: view.getFloat32(4, true), theta: view.getFloat32(8, true) };
}

export function decodePointPairs(value) {
    const bytes = asBytes(value); requireLength(bytes, 4, "point pairs");
    const view = dataView(bytes);
    const count = view.getUint32(0, true);
    requireLength(bytes, 4 + count * 8, "point pairs");
    const points = new Float32Array(count * 2);
    for (let i = 0; i < points.length; i++) points[i] = view.getFloat32(4 + i * 4, true);
    return points;
}

export function decodeCostmap(value) {
    const bytes = asBytes(value); requireLength(bytes, 20, "costmap");
    const view = dataView(bytes);
    const width = view.getUint32(0, true);
    const height = view.getUint32(4, true);
    requireLength(bytes, 20 + width * height, "costmap");
    return {
        width,
        height,
        resolution: view.getFloat32(8, true),
        originX: view.getFloat32(12, true),
        originY: view.getFloat32(16, true),
        data: new Uint8Array(bytes.buffer, bytes.byteOffset + 20, width * height)
    };
}

export function decodeOdometry(value) {
    const bytes = asBytes(value); requireLength(bytes, 20, "odometry");
    const view = dataView(bytes);
    return {
        x: view.getFloat32(0, true),
        y: view.getFloat32(4, true),
        theta: view.getFloat32(8, true),
        linearVelocity: view.getFloat32(12, true),
        angularVelocity: view.getFloat32(16, true)
    };
}

export function decodeCameraImage(value) {
    const bytes = asBytes(value); requireLength(bytes, 12, "camera image");
    const view = dataView(bytes);
    const width = view.getUint32(0, true);
    const height = view.getUint32(4, true);
    const step = view.getUint32(8, true);
    requireLength(bytes, 12 + step * height, "camera image");
    return { width, height, step, data: new Uint8Array(bytes.buffer, bytes.byteOffset + 12, step * height) };
}

export function drawBgr8ToCanvas(image, canvas) {
    canvas.width = image.width;
    canvas.height = image.height;
    const context = canvas.getContext("2d");
    const output = context.createImageData(image.width, image.height);
    for (let y = 0; y < image.height; y++) {
        for (let x = 0; x < image.width; x++) {
            const source = y * image.step + x * 3;
            const target = (y * image.width + x) * 4;
            output.data[target] = image.data[source + 2];
            output.data[target + 1] = image.data[source + 1];
            output.data[target + 2] = image.data[source];
            output.data[target + 3] = 255;
        }
    }
    context.putImageData(output, 0, 0);
}

async function toApiError(response) {
    let details = null;
    try {
        const text = await response.text();
        if (text) {
            try { details = JSON.parse(text); }
            catch { details = { message: text }; }
        }
    } catch { /* ignore parse errors */ }
    const code = details?.error || "";
    const message = details?.message || details?.detail || details?.title || response.statusText || `HTTP ${response.status}`;
    const correlationId = response.headers.get("X-Correlation-Id") || "";
    return new CWebApiError(message, { status: response.status, code, details, response, correlationId });
}

function requireSignalR(messagePack) {
    const signalR = globalThis.signalR;
    if (!signalR?.HubConnectionBuilder) {
        throw new Error("SignalR runtime not found. Load @microsoft/signalr 8.0.7 before using realtime clients.");
    }
    if (messagePack && !signalR.protocols?.msgpack?.MessagePackHubProtocol) {
        throw new Error("SignalR MessagePack runtime not found. Load @microsoft/signalr-protocol-msgpack 8.0.7.");
    }
    return signalR;
}

function waitForIceGathering(pc, timeoutMs) {
    if (pc.iceGatheringState === "complete") return Promise.resolve(true);
    return new Promise(resolve => {
        let done = false;
        const finish = result => {
            if (done) return;
            done = true;
            clearTimeout(timer);
            pc.removeEventListener("icegatheringstatechange", check);
            resolve(result);
        };
        const check = () => { if (pc.iceGatheringState === "complete") finish(true); };
        const timer = setTimeout(() => finish(false), timeoutMs);
        pc.addEventListener("icegatheringstatechange", check);
    });
}

function isMediaStream(value) {
    return typeof globalThis.MediaStream !== "undefined" && value instanceof globalThis.MediaStream;
}

function asBytes(value) {
    if (value instanceof Uint8Array) return value;
    if (value instanceof ArrayBuffer) return new Uint8Array(value);
    if (Array.isArray(value)) return Uint8Array.from(value);
    if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    throw new TypeError("Expected Uint8Array, ArrayBuffer, typed array, or number[]");
}
function dataView(bytes) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); }
function requireLength(bytes, minimum, label) {
    if (bytes.byteLength < minimum) throw new RangeError(`Invalid ${label} frame: ${bytes.byteLength} < ${minimum}`);
}
function finite(value) {
    const number = Number(value);
    if (!Number.isFinite(number)) throw new TypeError(`Expected finite number, got ${value}`);
    return number;
}
function enc(value) { return encodeURIComponent(String(value)); }
function hasHeader(headers, name) { return Object.keys(headers).some(key => key.toLowerCase() === name.toLowerCase()); }
