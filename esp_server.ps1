param(
    [int]$Port = 8080
)

$ErrorActionPreference = 'Stop'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Any, $Port)

function Send-DeviceCommand {
    param(
        [Net.Sockets.NetworkStream]$Stream,
        [string]$Command
    )

    $data = [Text.Encoding]::ASCII.GetBytes($Command)
    $Stream.Write($data, 0, $data.Length)
    $Stream.Flush()
    Write-Host "Sent: $Command" -ForegroundColor Cyan
}

try {
    try {
        $listener.Start()
    }
    catch {
        Write-Error "Cannot listen on TCP port $Port. Close the old server process or choose another port. $($_.Exception.Message)"
        return
    }

    Write-Host "Listening on 0.0.0.0:$Port" -ForegroundColor Green
    Write-Host 'Commands: LED ON, LED OFF, BUZZER ON, BUZZER OFF, HELP, QUIT'

    while ($true) {
        Write-Host 'Waiting for ESP connection...'
        $client = $listener.AcceptTcpClient()
        $client.NoDelay = $true
        $stream = $client.GetStream()
        $remote = $client.Client.RemoteEndPoint
        $receiveText = ''
        $inputText = ''

        Write-Host "Connected: $remote" -ForegroundColor Green
        Write-Host -NoNewline '> '

        try {
            while ($client.Connected) {
                if ($stream.DataAvailable) {
                    $buffer = [byte[]]::new(256)
                    $count = $stream.Read($buffer, 0, $buffer.Length)
                    if ($count -eq 0) {
                        break
                    }

                    $receiveText += [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
                    while (($lineEnd = $receiveText.IndexOf("`n")) -ge 0) {
                        $line = $receiveText.Substring(0, $lineEnd).Trim("`r")
                        $receiveText = $receiveText.Substring($lineEnd + 1)
                        if ($line.Length -gt 0) {
                            Write-Host "`rRX: $line" -ForegroundColor Yellow
                            Write-Host -NoNewline "> $inputText"
                        }
                    }
                }

                if ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    if ($key.Key -eq [ConsoleKey]::Enter) {
                        Write-Host
                        $command = $inputText.Trim().ToUpperInvariant()
                        $inputText = ''

                        switch ($command) {
                            'LED ON'      { Send-DeviceCommand $stream $command }
                            'LED OFF'     { Send-DeviceCommand $stream $command }
                            'BUZZER ON'   { Send-DeviceCommand $stream $command }
                            'BUZZER OFF'  { Send-DeviceCommand $stream $command }
                            'HELP'        { Write-Host 'LED ON | LED OFF | BUZZER ON | BUZZER OFF | QUIT' }
                            'QUIT'        { throw [OperationCanceledException]::new('Server stopped by user.') }
                            ''            { }
                            default       { Write-Host "Unknown command: $command" -ForegroundColor Red }
                        }
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

                if ($client.Client.Poll(0, [Net.Sockets.SelectMode]::SelectRead) -and
                    $client.Client.Available -eq 0) {
                    break
                }

                Start-Sleep -Milliseconds 20
            }
        }
        catch [OperationCanceledException] {
            throw
        }
        catch {
            Write-Host "`nConnection error: $($_.Exception.Message)" -ForegroundColor Red
        }
        finally {
            $stream.Dispose()
            $client.Dispose()
        }

        Write-Host "`nESP disconnected." -ForegroundColor DarkYellow
    }
}
catch [OperationCanceledException] {
    Write-Host "`nServer stopped."
}
finally {
    $listener.Stop()
}
