import * as signalRCore from '@microsoft/signalr'
import { MessagePackHubProtocol } from '@microsoft/signalr-protocol-msgpack'

export default defineNuxtPlugin(() => {
  /* SDK（app/lib/cwebapi）从 globalThis.signalR 读取运行时（后端接入文档 §1） */
  ;(globalThis as unknown as { signalR: unknown }).signalR = {
    ...signalRCore,
    protocols: { msgpack: { MessagePackHubProtocol } },
  }
})
