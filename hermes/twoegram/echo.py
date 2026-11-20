"""The echo agent, for the tests and the stand-alone dev server: answers every
message after a short pause by saying it back, in Markdown with a little
Unicode so the folding is exercised. A message containing "long" gets an
answer long enough to arrive in several parts. As openclaw/src/agent.ts."""

from __future__ import annotations

import asyncio
from typing import Optional


class EchoAgent:
    def __init__(self, names=("main", "coder"), delay: float = 0.02, labels: Optional[dict] = None):
        self.names = list(names)
        self.delay = delay
        self.labels = {"main": "Echo"} if labels is None else labels
        self.server = None                       # the A2Server, set once it exists

    def agents(self) -> list[str]:
        return self.names

    def display_name(self, agent: str) -> Optional[str]:
        return self.labels.get(agent)

    async def deliver(self, device: str, agent: str, text: str) -> None:
        async def later():
            await asyncio.sleep(self.delay)
            self.server.post(device, agent, f"**{agent}** heard you — “{text}” \U0001F44D")
            if "long" in text.lower():
                para = "This paragraph is here to make the answer long enough to be cut into parts. " * 6
                for i in range(1, 6):
                    self.server.post(device, agent, f"## Part {i}\n\n{para}")
                self.server.post(device, agent,
                                 "```\n        lda #$00\nloop:   sta $0200,y\n        iny\n        bne loop\n```")
            self.server.set_busy(device, agent, False)
        asyncio.get_running_loop().create_task(later())
