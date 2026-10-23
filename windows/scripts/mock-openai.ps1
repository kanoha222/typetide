# 翻译后端 mock：
#   POST */chat/completions → OpenAI 风格 SSE 流式
#   POST /api/generate      → Ollama 风格 NDJSON 流式
# 测试用：让 TypeTide 的两种后端都有可对接的本地端点。
# 用法: .\mock-openai.ps1 [-Port 8199] [-Reply "TRANSLATED_TEXT"] [-RecordFile path]
#
# 请求体里的 model 名触发测试行为（其他 model 保持原样，回复 -Reply）：
#   mock-exact         回复带缩进、制表符与结尾换行的固定文本
#   mock-reject-think  请求体含 think 字段时返回 HTTP 400
#   mock-redirect      返回 307 指向 /redirected/...（客户端不应跟随）
# -RecordFile：每个请求追加一行 JSON {path, body, auth}；auth 只记是否带了
# Authorization 头，从不记录其值。
param(
    [int]$Port = 8199,
    [string]$Reply = "MOCK_TRANSLATION_OK",
    [string]$RecordFile = ""
)
$ErrorActionPreference = "Stop"
$exactReply = "  EXACT_OUTPUT`n`tline 2`n"
$utf8 = New-Object System.Text.UTF8Encoding($false)   # 无 BOM

function Send-Text($res, [int]$status, [string]$text) {
    $res.StatusCode = $status
    $bytes = $utf8.GetBytes($text)
    $res.OutputStream.Write($bytes, 0, $bytes.Length)
    $res.Close()
}

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$listener.Start()
Write-Output "mock backend listening on http://127.0.0.1:$Port"
while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    $res = $ctx.Response
    $raw = (New-Object System.IO.StreamReader($req.InputStream, $utf8)).ReadToEnd()
    $path = $req.Url.AbsolutePath
    Write-Output "request: $($req.HttpMethod) $path"
    if ($RecordFile) {
        $entry = [ordered]@{ path = $path; body = $raw; auth = [bool]$req.Headers["Authorization"] }
        [System.IO.File]::AppendAllText($RecordFile, ($entry | ConvertTo-Json -Compress) + "`n", $utf8)
    }
    $model = ""; $hasThink = $false
    try {
        $parsed = $raw | ConvertFrom-Json
        $model = [string]$parsed.model
        $hasThink = $null -ne $parsed.PSObject.Properties['think']
    } catch {}

    $isOpenAI = $path -like "*/chat/completions"
    $isOllama = $path -eq "/api/generate"
    if (-not ($isOpenAI -or $isOllama)) {
        $res.StatusCode = 404; $res.Close(); continue
    }
    if ($model -eq "mock-redirect" -and -not $path.StartsWith("/redirected")) {
        $res.StatusCode = 307
        $res.RedirectLocation = "http://127.0.0.1:$Port/redirected$path"
        $res.Close(); continue
    }
    if ($model -eq "mock-reject-think" -and $hasThink) {
        Send-Text $res 400 '{"error":"think is not supported"}'
        continue
    }
    $text = if ($model -eq "mock-exact") { $exactReply } else { $Reply }

    $res.StatusCode = 200
    $res.ContentType = if ($isOpenAI) { "text/event-stream" } else { "application/x-ndjson" }
    $res.SendChunked = $true
    $writer = New-Object System.IO.StreamWriter($res.OutputStream, $utf8)
    # 把回复拆成两个 delta 模拟流式
    $half = [int][Math]::Ceiling($text.Length / 2)
    foreach ($part in @($text.Substring(0, $half), $text.Substring($half))) {
        if ($isOpenAI) {
            $json = '{"choices":[{"delta":{"content":' + (ConvertTo-Json $part) + '}}]}'
            $writer.Write("data: $json`n`n")
        } else {
            $json = '{"response":' + (ConvertTo-Json $part) + ',"done":false}'
            $writer.Write("$json`n")
        }
        $writer.Flush()
        Start-Sleep -Milliseconds 60
    }
    if ($isOpenAI) { $writer.Write("data: [DONE]`n`n") }
    else { $writer.Write('{"response":"","done":true}' + "`n") }
    $writer.Flush()
    $res.Close()
}
