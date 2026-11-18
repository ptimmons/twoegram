// Reading a key typed at the gateway host without showing it. On a
// terminal, the characters are not echoed; otherwise (a pipe, a test) one
// line is read from stdin. Never from an argument: that would leave the
// key in the shell's history and in the process list.

import { createInterface } from "node:readline";

export function readSecret(prompt: string): Promise<string> {
  const input = process.stdin;
  const output = process.stderr;
  output.write(prompt);
  if (!input.isTTY) {
    return new Promise((resolve) => {
      const rl = createInterface({ input, terminal: false });
      let done = false;
      const finish = (line: string) => {
        if (done) return;
        done = true;
        rl.close();
        output.write("\n");
        resolve(line);
      };
      rl.once("line", finish);
      rl.once("close", () => finish(""));
    });
  }
  return new Promise((resolve, reject) => {
    let typed = "";
    const wasRaw = input.isRaw;
    input.setRawMode(true);
    input.resume();
    input.setEncoding("utf8");
    const onData = (chunk: string) => {
      for (const ch of chunk) {
        if (ch === "\r" || ch === "\n" || ch === "\u0004") {
          stop();
          output.write("\n");
          resolve(typed);
          return;
        }
        if (ch === "\u0003") {
          stop();
          output.write("\n");
          reject(new Error("interrupted"));
          return;
        }
        if (ch === "\u007f" || ch === "\b") typed = typed.slice(0, -1);
        else if (ch >= " ") typed += ch;
      }
    };
    const stop = () => {
      input.off("data", onData);
      input.setRawMode(wasRaw);
      input.pause();
    };
    input.on("data", onData);
  });
}
