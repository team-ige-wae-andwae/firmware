# CSI firmware control protocol

This branch adds runtime control of the CSI sender's packet rate and Wi-Fi
transmit-power ceiling without changing the existing four-byte sequence payload
or the receiver's `CSI_DATA`/gain output.

## Defaults and limits

- Default packet rate: `40 Hz`
- Allowed packet rate: `1..200 Hz`
- Default TX power ceiling: `80 qdBm` (`20 dBm`)
- Allowed TX power ceiling: `8..80 qdBm` (`2..20 dBm`)
- `qdBm` is the unit required by ESP-IDF: `qdBm / 4 = dBm`

The TX power value is a configured upper limit. The radio can use a lower
instantaneous power because of regulatory, calibration, or PHY constraints.

## Gateway to RX UART commands

Commands are newline-terminated ASCII at the receiver's configured UART baud
rate (`921600`). `request_id` is an unsigned 32-bit number selected by the
gateway.

```text
CSI_CTRL,SET,<request_id>,<rate_hz>,<tx_power_qdbm>
CSI_CTRL,GET,<request_id>
CSI_CTRL,RESET,<request_id>
```

Examples:

```text
CSI_CTRL,SET,1001,40,60
CSI_CTRL,GET,1002
CSI_CTRL,RESET,1003
```

The first example selects 40 packets/s and a 15 dBm ceiling (`60 / 4`). `SET`
and `RESET` persist their values in the TX NVS partition. `RESET` restores
40 Hz and 20 dBm.

## RX UART responses

The RX first reports that an accepted command was transmitted over ESP-NOW:

```text
CSI_CTRL_SENT,<request_id>,0,<requested_rate_hz>,<requested_tx_power_qdbm>
```

The TX then returns the applied values, which the RX forwards to UART:

```text
CSI_CTRL_ACK,<request_id>,<esp_err>,<actual_rate_hz>,<actual_tx_power_qdbm>
```

`esp_err` is `0` on success. A gateway should treat `CSI_CTRL_SENT` only as an
intermediate state and wait for the matching `CSI_CTRL_ACK`. It should time out
if the ACK is not received, because ESP-NOW broadcast delivery is not
guaranteed.

At boot the RX advertises the protocol version and limits:

```text
CSI_CTRL_READY,<version>,<min_rate>,<max_rate>,<min_power_qdbm>,<max_power_qdbm>,<rx_mac>
```

## ESP-NOW wire format

RX and TX exchange a packed, versioned 20-byte `csi_ctrl_packet_t`. The shared
definition is `main/csi_control_protocol.h` in both projects. Control packets
use magic `CSIC`, and the RX excludes TX status frames from the sensing dataset.

The RX retains its factory MAC so that bidirectional control works. The TX
retains the fixed CSI sender MAC `1a:00:00:00:00:00`, preserving the existing
CSI source filter.
