// What the plugin needs from whatever answers: inside OpenClaw (milestone
// 6) the gateway's agents; stand-alone, the echo agent below.

export interface ReplySink {
  /** Some of the answer, as the agent sends it. May be called often. */
  text(t: string): void;
  /** The agent has finished this turn. */
  done(): void;
}

export interface AgentPort {
  agents(): string[];
  /** What the agent calls itself, if anything besides its id. */
  displayName?(agent: string): string | undefined;
  deliver(device: string, agent: string, text: string, out: ReplySink): void;
}

/**
 * Answers every message after a short pause by saying it back, in Markdown
 * with a little Unicode so the folding is exercised. A message containing
 * "long" gets an answer long enough to arrive in several parts.
 */
export class EchoAgent implements AgentPort {
  private names: string[];
  private delayMs: number;
  private labels: Record<string, string>;

  constructor(names = ["main", "coder"], delayMs = 800, labels: Record<string, string> = { main: "Echo" }) {
    this.names = names;
    this.delayMs = delayMs;
    this.labels = labels;
  }

  agents(): string[] {
    return this.names;
  }

  displayName(agent: string): string | undefined {
    return this.labels[agent];
  }

  deliver(_device: string, agent: string, text: string, out: ReplySink): void {
    setTimeout(() => {
      out.text(`**${agent}** heard you — “${text}” \u{1F44D}`);
      if (/long/i.test(text)) {
        const para = "This paragraph is here to make the answer long enough to be cut into parts. ".repeat(6);
        for (let i = 1; i <= 5; i++) out.text(`## Part ${i}\n\n${para}`);
        out.text("```\n        lda #$00\nloop:   sta $0200,y\n        iny\n        bne loop\n```");
      }
      out.done();
    }, this.delayMs);
  }
}
