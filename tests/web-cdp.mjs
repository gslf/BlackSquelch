/* Tiny DevTools client built entirely on Node's standard APIs. */
export class DevTools {
  constructor(socket) {
    this.socket = socket; this.sequence = 0; this.pending = new Map(); this.events = [];
    socket.addEventListener('message', ({data}) => {
      const value = JSON.parse(data);
      if (value.id) {
        const pending = this.pending.get(value.id); this.pending.delete(value.id);
        if (value.error) pending.reject(new Error(JSON.stringify(value.error)));
        else pending.resolve(value.result);
      } else this.events.push(value);
    });
  }
  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, {once: true}); socket.addEventListener('error', reject, {once: true}); });
    return new DevTools(socket);
  }
  call(method, params = {}) {
    const id = ++this.sequence;
    return new Promise((resolve, reject) => { this.pending.set(id, {resolve, reject}); this.socket.send(JSON.stringify({id, method, params})); });
  }
  async evaluate(expression) {
    const result = await this.call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true, userGesture: true});
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
    return result.result.value;
  }
  async wait(expression, timeout = 20000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      try { if (await this.evaluate(expression)) return; }
      catch (error) {
        if (!/navigated|Cannot find context|Execution context was destroyed/.test(error.message)) throw error;
      }
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    throw new Error(`Timed out: ${expression}\n${await this.evaluate('document.body.innerText')}`);
  }
  close() { this.socket.close(); }
}
