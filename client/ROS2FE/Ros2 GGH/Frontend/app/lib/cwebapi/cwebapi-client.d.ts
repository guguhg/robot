export type BinaryValue = ArrayBuffer | ArrayBufferView | number[];

export interface CWebApiClientOptions {
    baseUrl?: string;
    accessToken?: string;
    tokenProvider?: (() => string | null | undefined) | null;
    fetchImpl?: typeof fetch;
    onUnauthorized?: ((error: CWebApiError) => void | Promise<void>) | null;
}

export interface RequestOptions {
    method?: string;
    body?: unknown;
    headers?: Record<string, string>;
    auth?: boolean;
    signal?: AbortSignal;
}

export interface LoginResponse {
    token: string;
    expiresAt: string;
    username: string;
    email: string;
    permissions: string[];
    isGuest: boolean;
}

export interface RegisterResponse {
    userId: string;
    username: string;
    email: string;
    emailVerificationRequired: boolean;
    verificationEmailSent: boolean;
}

export interface DeviceInfo {
    deviceId: string;
    name: string;
    isOnline: boolean;
}

export interface StreamUrlResult {
    app: string;
    stream: string;
    whepUrl: string;
    whipUrl: string;
    token: string;
    expiresAt: string;
}

export interface PointCloudStreamInfo {
    name: string;
    topic: string;
    messageType: string;
    maxPoints: number;
}

export interface OperatorState {
    hasOperator: boolean;
    userId: string | null;
    username: string | null;
    isSelf: boolean;
}

export interface OperatorChanged {
    userId: string | null;
    username: string | null;
}

export interface Assignee {
    userId: string;
    username: string;
    email: string;
    isOperator: boolean;
}

export interface ChatMessage {
    author: string;
    text: string;
    ts: string;
}

export interface PointCloudFrame {
    count: number;
    xyz: Float32Array;
}

export interface MapFrame {
    width: number;
    height: number;
    resolution: number;
    originX: number;
    originY: number;
    originYaw: number;
    data: Int8Array;
}

export interface RobotPose {
    x: number;
    y: number;
    theta: number;
}

export interface CostmapFrame {
    width: number;
    height: number;
    resolution: number;
    originX: number;
    originY: number;
    data: Uint8Array;
}

export interface OdometryFrame {
    x: number;
    y: number;
    theta: number;
    linearVelocity: number;
    angularVelocity: number;
}

export interface CameraImageFrame {
    width: number;
    height: number;
    step: number;
    data: Uint8Array;
}

export interface Velocity {
    linearX: number;
    linearY?: number;
    angularZ: number;
}

export interface MapHubHandlers {
    onFrameError?: (event: string, error: unknown, raw: BinaryValue) => void;
    onMap?: (value: MapFrame, raw: Uint8Array) => void;
    onRobotPose?: (value: RobotPose, raw: Uint8Array) => void;
    onParticles?: (value: Float32Array, raw: Uint8Array) => void;
    onPath?: (value: Float32Array, raw: Uint8Array) => void;
    onLocalPlan?: (value: Float32Array, raw: Uint8Array) => void;
    onScan?: (value: Float32Array, raw: Uint8Array) => void;
    onGlobalCostmap?: (value: CostmapFrame, raw: Uint8Array) => void;
    onLocalCostmap?: (value: CostmapFrame, raw: Uint8Array) => void;
    onMapPatch?: (value: CostmapFrame, raw: Uint8Array) => void;
    onBatterySoc?: (value: number) => void;
    onBatteryVoltage?: (value: number) => void;
    onSafeTwist?: (linear: number, angular: number) => void;
    onOdometry?: (value: OdometryFrame, raw: Uint8Array) => void;
    onCameraImage?: (value: CameraImageFrame, raw: Uint8Array) => void;
}

export interface WhepPlayerOptions {
    rtcConfiguration?: RTCConfiguration;
    iceGatheringTimeoutMs?: number;
    onStateChange?: (state: RTCPeerConnectionState) => void;
}

export interface WhipPublisherOptions {
    rtcConfiguration?: RTCConfiguration;
    iceGatheringTimeoutMs?: number;
    onStateChange?: (state: RTCPeerConnectionState) => void;
}

export class CWebApiError extends Error {
    status: number;
    code: string;
    details: unknown;
    response: Response | null;
    correlationId: string;
    constructor(message: string, options?: {
        status?: number;
        code?: string;
        details?: unknown;
        response?: Response | null;
        correlationId?: string;
    });
}

export class CWebApiClient {
    baseUrl: string;
    constructor(options?: CWebApiClientOptions);
    setAccessToken(token: string): void;
    clearAccessToken(): void;
    getAccessToken(): string;
    request<T = unknown>(path: string, options?: RequestOptions): Promise<T>;
    url(path: string): string;

    login(identifier: string, password: string, options?: RequestOptions): Promise<LoginResponse>;
    guestLogin(options?: RequestOptions): Promise<LoginResponse>;
    register(username: string, email: string, password: string, options?: RequestOptions): Promise<RegisterResponse>;
    verifyEmail(email: string, code: string, options?: RequestOptions): Promise<void>;
    resendVerification(email: string, options?: RequestOptions): Promise<void>;
    forgotPassword(email: string, options?: RequestOptions): Promise<void>;
    resetPassword(email: string, code: string, newPassword: string, options?: RequestOptions): Promise<void>;

    getHealth(options?: RequestOptions): Promise<{ status: string }>;
    getReady(options?: RequestOptions): Promise<{ status: string; database: string }>;
    getDevices(options?: RequestOptions): Promise<DeviceInfo[]>;
    getDevice(deviceId: string, options?: RequestOptions): Promise<DeviceInfo>;
    getStreamUrl(deviceId: string, options?: RequestOptions): Promise<StreamUrlResult>;
    getPublishUrl(deviceId: string, options?: RequestOptions): Promise<StreamUrlResult>;
    getPointCloudStreams(options?: RequestOptions): Promise<PointCloudStreamInfo[]>;

    getCurrentOperator(options?: RequestOptions): Promise<OperatorState>;
    getOperatorUsers(options?: RequestOptions): Promise<Assignee[]>;
    assignOperator(userId: string, options?: RequestOptions): Promise<OperatorState>;
    reclaimOperator(options?: RequestOptions): Promise<OperatorState>;

    buildHub(path: string, options?: { messagePack?: boolean; automaticReconnect?: boolean }): any;
    createPointCloudHub(options?: { automaticReconnect?: boolean }): PointCloudHubClient;
    createMapHub(handlers?: MapHubHandlers, options?: { automaticReconnect?: boolean }): MapHubClient;
    createRobotControl(options?: { hz?: number; onError?: ((error: unknown) => void) | null }): RobotControlClient;
    createChatHub(options?: { automaticReconnect?: boolean }): ChatHubClient;
    createOperatorClient(options?: { automaticReconnect?: boolean }): OperatorClient;
    createWhepPlayer(videoElement: HTMLVideoElement, options?: WhepPlayerOptions): WhepPlayer;
    createWhipPublisher(options?: WhipPublisherOptions): WhipPublisher;
}

export class PointCloudHubClient {
    connection: any;
    constructor(api: CWebApiClient, options?: { automaticReconnect?: boolean });
    onFrame(handler: (stream: string, frame: PointCloudFrame, raw: Uint8Array) => void): () => boolean;
    start(): Promise<void>;
    stop(): Promise<void>;
    subscribe(stream: string): Promise<void>;
    unsubscribe(stream: string): Promise<void>;
}

export class MapHubClient {
    connection: any;
    constructor(api: CWebApiClient, handlers?: MapHubHandlers, options?: { automaticReconnect?: boolean });
    setHandlers(handlers?: MapHubHandlers): this;
    start(): Promise<void>;
    stop(): Promise<void>;
    subscribe(): Promise<void>;
    setGoal(x: number, y: number, theta: number): Promise<void>;
    setInitialPose(x: number, y: number, theta: number): Promise<void>;
}

export class RobotControlClient {
    connection: any;
    hz: number;
    constructor(api: CWebApiClient, options?: { hz?: number; onError?: ((error: unknown) => void) | null });
    start(): Promise<void>;
    stop(): Promise<void>;
    setVelocity(linearX: number, linearY: number, angularZ: number): Promise<void>;
    stopRobot(): Promise<void>;
    beginVelocity(provider: () => Velocity, hz?: number): void;
    stopVelocityLoop(sendStop?: boolean): Promise<void>;
    dispose(): Promise<void>;
}

export class ChatHubClient {
    connection: any;
    constructor(api: CWebApiClient, options?: { automaticReconnect?: boolean });
    onMessage(handler: (message: ChatMessage) => void): () => boolean;
    onHistory(handler: (messages: ChatMessage[]) => void): () => boolean;
    onOperatorChanged(handler: (change: OperatorChanged) => void): () => boolean;
    start(): Promise<void>;
    stop(): Promise<void>;
    sendMessage(text: string): Promise<void>;
}

export class OperatorClient {
    connection: any;
    constructor(api: CWebApiClient, options?: { automaticReconnect?: boolean });
    onChanged(handler: (state: OperatorState) => void): () => boolean;
    start(): Promise<void>;
    stop(): Promise<void>;
    getCurrent(options?: RequestOptions): Promise<OperatorState>;
    listUsers(options?: RequestOptions): Promise<Assignee[]>;
    assign(userId: string, options?: RequestOptions): Promise<OperatorState>;
    reclaim(options?: RequestOptions): Promise<OperatorState>;
}

export class WhepPlayer {
    peerConnection: RTCPeerConnection | null;
    constructor(api: CWebApiClient, videoElement: HTMLVideoElement, options?: WhepPlayerOptions);
    start(deviceId: string): Promise<StreamUrlResult>;
    stop(): void;
}

export class WhipPublisher {
    peerConnection: RTCPeerConnection | null;
    mediaStream: MediaStream | null;
    constructor(api: CWebApiClient, options?: WhipPublisherOptions);
    start(deviceId: string, streamOrConstraints?: MediaStream | MediaStreamConstraints): Promise<StreamUrlResult>;
    stop(): void;
}

export function decodePointCloudFrame(value: BinaryValue): PointCloudFrame;
export function decodeMapFrame(value: BinaryValue): MapFrame;
export function decodeRobotPose(value: BinaryValue): RobotPose;
export function decodePointPairs(value: BinaryValue): Float32Array;
export function decodeCostmap(value: BinaryValue): CostmapFrame;
export function decodeOdometry(value: BinaryValue): OdometryFrame;
export function decodeCameraImage(value: BinaryValue): CameraImageFrame;
export function drawBgr8ToCanvas(image: CameraImageFrame, canvas: HTMLCanvasElement): void;
