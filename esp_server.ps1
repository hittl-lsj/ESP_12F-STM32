param(
    [string]$Broker = '127.0.0.1',
    [int]$Port = 1883,
    [string]$ClientId = "esp-console-$([Guid]::NewGuid().ToString('N').Substring(0, 8))",
    [string]$Username = '',
    [string]$Password = '',
    [string]$StatusTopic = 'stm32/esp12f/status',
    [string]$CommandTopic = 'stm32/esp12f/command'
)

$ErrorActionPreference = 'Stop'
$script:PacketId = 1

function Add-MqttString {
    param(
        [Collections.Generic.List[byte]]$Buffer,
        [string]$Value
    )

    $data = [Text.Encoding]::UTF8.GetBytes($Value)
    $Buffer.Add([byte]($data.Length -shr 8))
    $Buffer.Add([byte]($data.Length -band 0xFF))
    $Buffer.AddRange([byte[]]$data)
}

function Add-MqttRemainingLength {
    param(
        [Collections.Generic.List[byte]]$Buffer,
        [int]$Length
    )

    do {
        $digit = $Length % 128
        $Length = [Math]::Floor($Length / 128)
        if ($Length -gt 0) { $digit = $digit -bor 0x80 }
        $Buffer.Add([byte]$digit)
    } while ($Length -gt 0)
}

function New-MqttConnectPacket {
    $body = [Collections.Generic.List[byte]]::new()
    Add-MqttString $body 'MQTT'
    $body.Add(4)
    $flags = 0x02
    if ($Username.Length -gt 0) { $flags = $flags -bor 0x80 }
    if ($Password.Length -gt 0) { $flags = $flags -bor 0x40 }
    $body.Add([byte]$flags)
    $body.Add(0)
    $body.Add(30)
    Add-MqttString $body $ClientId
    if ($Username.Length -gt 0) { Add-MqttString $body $Username }
    if ($Password.Length -gt 0) { Add-MqttString $body $Password }

    $packet = [Collections.Generic.List[byte]]::new()
    $packet.Add(0x10)
    Add-MqttRemainingLength $packet $body.Count
    $packet.AddRange($body.ToArray())
    return ,$packet.ToArray()
}

function New-MqttSubscribePacket {
    param([string]$Topic)

    $body = [Collections.Generic.List[byte]]::new()
    $id = $script:PacketId++
    $body.Add([byte]($id -shr 8))
    $body.Add([byte]($id -band 0xFF))
    Add-MqttString $body $Topic
    $body.Add(0)

    $packet = [Collections.Generic.List[byte]]::new()
    $packet.Add(0x82)
    Add-MqttRemainingLength $packet $body.Count
    $packet.AddRange($body.ToArray())
    return ,$packet.ToArray()
}

function New-MqttPublishPacket {
    param(
        [string]$Topic,
        [string]$Message
    )

    $body = [Collections.Generic.List[byte]]::new()
    Add-MqttString $body $Topic
    $body.AddRange([Text.Encoding]::UTF8.GetBytes($Message))
    $packet = [Collections.Generic.List[byte]]::new()
    $packet.Add(0x30)
    Add-MqttRemainingLength $packet $body.Count
    $packet.AddRange($body.ToArray())
    return ,$packet.ToArray()
}

function Send-MqttPacket {
    param(
        [Net.Sockets.NetworkStream]$Stream,
        [byte[]]$Packet
    )

    $Stream.Write($Packet, 0, $Packet.Length)
    $Stream.Flush()
}

function Read-MqttExact {
    param(
        [Net.Sockets.NetworkStream]$Stream,
        [int]$Count
    )

    $result = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($result, $offset, $Count - $offset)
        if ($read -eq 0) { throw 'Broker closed the connection.' }
        $offset += $read
    }
    return ,$result
}

function Read-MqttPacket {
    param([Net.Sockets.NetworkStream]$Stream)

    $header = (Read-MqttExact $Stream 1)[0]
    $remaining = 0
    $multiplier = 1
    do {
        $digit = (Read-MqttExact $Stream 1)[0]
        $remaining += ($digit -band 0x7F) * $multiplier
        $multiplier *= 128
        if ($multiplier -gt 268435456) { throw 'Invalid MQTT remaining length.' }
    } while (($digit -band 0x80) -ne 0)

    $body = if ($remaining -gt 0) { Read-MqttExact $Stream $remaining } else { [byte[]]::new(0) }
    return [PSCustomObject]@{ Type = $header -shr 4; Header = $header; Body = $body }
}

function Show-MqttPacket {
    param($Packet)

    if ($Packet.Type -ne 3 -or $Packet.Body.Length -lt 2) { return }
    $topicLength = ($Packet.Body[0] -shl 8) -bor $Packet.Body[1]
    if ($Packet.Body.Length -lt (2 + $topicLength)) { return }
    $topic = [Text.Encoding]::UTF8.GetString($Packet.Body, 2, $topicLength)
    $payloadOffset = 2 + $topicLength
    if (($Packet.Header -band 0x06) -ne 0) { $payloadOffset += 2 }
    if ($payloadOffset -gt $Packet.Body.Length) { return }
    $message = [Text.Encoding]::UTF8.GetString(
        $Packet.Body, $payloadOffset, $Packet.Body.Length - $payloadOffset)
    Write-Host "`rRX [$topic]: $message" -ForegroundColor Yellow
}

function Publish-DeviceCommand {
    param(
        [Net.Sockets.NetworkStream]$Stream,
        [string]$Command
    )

    Send-MqttPacket $Stream (New-MqttPublishPacket $CommandTopic $Command)
    Write-Host "Published [$CommandTopic]: $Command" -ForegroundColor Cyan
}

$client = $null
$stream = $null
try {
    $client = [Net.Sockets.TcpClient]::new()
    Write-Host "Connecting to MQTT broker ${Broker}:$Port ..."
    $client.Connect($Broker, $Port)
    $client.NoDelay = $true
    $stream = $client.GetStream()
    $stream.ReadTimeout = 5000

    Send-MqttPacket $stream (New-MqttConnectPacket)
    $connack = Read-MqttPacket $stream
    if (($connack.Type -ne 2) -or ($connack.Body.Length -lt 2) -or ($connack.Body[1] -ne 0)) {
        $code = if ($connack.Body.Length -ge 2) { $connack.Body[1] } else { -1 }
        throw "MQTT connection rejected (CONNACK code $code)."
    }

    Send-MqttPacket $stream (New-MqttSubscribePacket $StatusTopic)
    $suback = Read-MqttPacket $stream
    if ($suback.Type -ne 9) { throw 'Broker did not return SUBACK.' }

    $stream.ReadTimeout = 30000
    Write-Host "Connected. Subscribed to: $StatusTopic" -ForegroundColor Green
    Write-Host 'Commands: LED ON, LED OFF, BUZZER ON, BUZZER OFF, HELP, QUIT'
    Write-Host -NoNewline '> '
    $inputText = ''
    $lastSend = [DateTime]::UtcNow

    while ($client.Connected) {
        if ($stream.DataAvailable) {
            Show-MqttPacket (Read-MqttPacket $stream)
            Write-Host -NoNewline "> $inputText"
        }

        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq [ConsoleKey]::Enter) {
                Write-Host
                $command = $inputText.Trim().ToUpperInvariant()
                $inputText = ''
                switch ($command) {
                    'LED ON'     { Publish-DeviceCommand $stream $command; $lastSend = [DateTime]::UtcNow }
                    'LED OFF'    { Publish-DeviceCommand $stream $command; $lastSend = [DateTime]::UtcNow }
                    'BUZZER ON'  { Publish-DeviceCommand $stream $command; $lastSend = [DateTime]::UtcNow }
                    'BUZZER OFF' { Publish-DeviceCommand $stream $command; $lastSend = [DateTime]::UtcNow }
                    'HELP'       { Write-Host 'LED ON | LED OFF | BUZZER ON | BUZZER OFF | QUIT' }
                    'QUIT'       { break }
                    ''           { }
                    default      { Write-Host "Unknown command: $command" -ForegroundColor Red }
                }
                if ($command -eq 'QUIT') { break }
                Write-Host -NoNewline '> '
            }
            elseif ($key.Key -eq [ConsoleKey]::Backspace) {
                if ($inputText.Length -gt 0) {
                    $inputText = $inputText.Substring(0, $inputText.Length - 1)
                    Write-Host -NoNewline "`b `b"
                }
            }
            elseif (-not [char]::IsControl($key.KeyChar)) {
                $inputText += $key.KeyChar
                Write-Host -NoNewline $key.KeyChar
            }
        }

        if (([DateTime]::UtcNow - $lastSend).TotalSeconds -ge 15) {
            Send-MqttPacket $stream ([byte[]](0xC0, 0x00))
            $lastSend = [DateTime]::UtcNow
        }
        if ($client.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead) -and
            $client.Client.Available -eq 0) {
            throw 'Broker closed the connection.'
        }
        Start-Sleep -Milliseconds 20
    }
}
catch {
    Write-Host "`nMQTT error: $($_.Exception.Message)" -ForegroundColor Red
}
finally {
    if ($null -ne $stream) { $stream.Dispose() }
    if ($null -ne $client) { $client.Dispose() }
    Write-Host "`nMQTT console stopped."
}
