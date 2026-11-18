# The Twoegram relay

One a2 gateway for the Apple, with the agents of several gateways behind
it (docs/RELAY.md, part 2). The Apple pairs with the relay exactly as
with any gateway; the relay pairs with each gateway exactly as an Apple
does. Python 3.11 or later, standard library only.

- **Pass-through.** Send, poll and history go to the agent's gateway and
  come back as that gateway sent them, signed again for the Apple. The
  relay keeps no conversations.
- **One list.** The Apple sees every gateway's agents, under their own
  ids. Where two clash, the later one takes its gateway's name, such as
  `main-mini`, unless `upstreams.json` gives an alias. The Apple lists
  8 at most.
- **Unread marks** for the gateways the Apple is not using come from a
  quiet poll every 20 seconds, while an Apple has been heard from in the
  last 10 minutes.
- **Signed, not encrypted**, like everything else here. LAN only: never
  forward its port to the internet.
- **Shared conversations.** Each gateway sees the relay as one device.
  So conversations through the relay start empty, and two Apples paired
  with the same relay would share them.

## On a Proxmox LXC

1. **The container:** unprivileged, Ubuntu LTS from Proxmox's
   templates, with a fixed address on the LAN. 1 core, 256 MB and 2 GB
   are plenty. It needs `python3`, which the template has.
2. **The relay.** On the Mac:

   ```bash
   make relay-dist
   ```

   ```bash
   scp build/twoegram-relay-0.1.0.tgz root@RELAY-HOST:
   ```

   On the container, as root:

   ```bash
   tar xzf twoegram-relay-0.1.0.tgz && twoegram-relay/install.sh
   ```

   The service runs as its own user, `twoegram-relay`, from
   `/opt/twoegram-relay`. Its files are in `/etc/twoegram-relay` and
   `/var/lib/twoegram-relay`, and only it can read them. systemd keeps
   the rest of the system read-only to it.
3. **Each gateway.** On the gateway, pair the relay like an Apple:

   ```bash
   openclaw a2 pair "relay"
   ```

   Then, on the container, give the relay that key (asked for, not
   shown):

   ```bash
   sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay upstream add claw1 192.168.1.20:8417
   ```

   It says hello to check, and lists the agents it found. For Hermes,
   `hermes a2 pair "relay"` on the Mac mini, then `upstream add mini
   192.168.1.30:8417`.
4. **Start it:**

   ```bash
   systemctl enable --now twoegram-relay
   ```

   ```bash
   journalctl -u twoegram-relay -f
   ```

   The log says `relay: listening on http://0.0.0.0:8417/a2/` and the
   agents found.
5. **The Apple.** In Twoegram's set-up: a key name for the relay, such
   as `relay`, the container's address, then Open-Apple-V. On the
   container:

   ```bash
   sudo -u twoegram-relay /opt/twoegram-relay/twoegram-relay add "Apple IIe"
   ```

   The device id it prints should be the Apple's. Return on the Apple:
   every gateway's agents in one list.

## The firewall

In Proxmox, on the container's network device (Firewall on, input
policy DROP, output policy DROP), allow only:

| Direction | What | Why |
|---|---|---|
| in | TCP 8417 from the Apple's address | the Apple |
| in | TCP 22 from the Mac | administration |
| out | TCP to each gateway's a2 port (192.168.1.20:8417, 192.168.1.30:8417) | the gateways |
| out | UDP/TCP 53 to the LAN's DNS, TCP 80 and 443 | only while updating; turn off after |

## Day to day

| | |
|---|---|
| `twoegram-relay list`, `revoke <id>` | the Apples (take effect at their next request) |
| `twoegram-relay upstream list` | the gateways, and whether each answers now |
| `twoegram-relay upstream remove <name>` | then revoke the relay on that gateway too |
| `systemctl restart twoegram-relay` | after changing the gateways |

Prefix each command with `sudo -u twoegram-relay /opt/twoegram-relay/`.
No command prints a stored key.

**Aliases**, in `/etc/twoegram-relay/upstreams.json`: `"aliases":
{"main": "pinchy"}` on claw1's entry lists its `main` as `pinchy`. Then
restart the relay.

**Updating:** unpack a new tarball and run `install.sh` again. Then
`systemctl restart twoegram-relay`.

## When a gateway is down

Its agents stay in the list. A request to one gets a note on the Apple:
`mini is not answering`, or `mini refused the relay's key` after the
relay has been revoked there. The relay tries the gateway again every
minute.

## Development

```bash
make relay-test
```

The tests run two real a2 servers with echo agents on this machine, and
a client that checks everything the way the Apple does. From the
repository, `relay/twoegram-relay` runs as is: the shared modules come
from `hermes/twoegram`. `TWOEGRAM_RELAY_CONFIG` and
`TWOEGRAM_RELAY_STATE` point it at scratch folders.
