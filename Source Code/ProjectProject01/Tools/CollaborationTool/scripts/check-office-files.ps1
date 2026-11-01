# Opens the sample files from check-office-files.ts in real Word, Excel and PowerPoint (read-only, hidden) and reads
# back what they see. An Office app that was already running is left running with its own documents untouched.
param([string]$Folder = "outputs/office-check")
$ErrorActionPreference = 'Stop'
$Folder = (Resolve-Path $Folder).Path
$results = [ordered]@{}
function Running($name) { [bool](Get-Process -Name $name -ErrorAction SilentlyContinue) }

$wasRunning = Running 'WINWORD'
$word = New-Object -ComObject Word.Application
try {
  $word.Visible = $false; $word.DisplayAlerts = 0
  $doc = $word.Documents.Open((Join-Path $Folder 'record.docx'), $false, $true, $false)
  $text = $doc.Content.Text
  $results.word = [ordered]@{ paragraphs = $doc.Paragraphs.Count; title = $doc.Paragraphs.Item(1).Range.Text.Trim(); heading = [bool]($text -match '회의록 "주간" & <점검>'); bullet = [bool]($text -match '•\s*맵 조명 마감'); style2 = $doc.Paragraphs.Item(3).Style.NameLocal }
  $doc.Close(0)
} finally { if (-not $wasRunning -and $word.Documents.Count -eq 0) { $word.Quit() }; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($word) }

$wasRunning = Running 'EXCEL'
$excel = New-Object -ComObject Excel.Application
try {
  $excel.Visible = $false; $excel.DisplayAlerts = $false
  $book = $excel.Workbooks.Open((Join-Path $Folder 'records.xlsx'), 0, $true)
  $sheet = $book.Worksheets.Item(1)
  $results.excel = [ordered]@{ sheets = $book.Worksheets.Count; second = $book.Worksheets.Item(2).Name; header = $sheet.Range('A1').Text; bold = $sheet.Range('A1').Font.Bold; formulaText = $sheet.Range('D2').Text; hasFormula = $sheet.Range('D2').HasFormula; leadingZero = $sheet.Range('A4').Text; filter = $sheet.AutoFilterMode }
  $csv = $excel.Workbooks.Open((Join-Path $Folder 'records.csv'), 0, $true)
  $results.csv = [ordered]@{ a2 = $csv.Worksheets.Item(1).Range('A2').Text; b2 = $csv.Worksheets.Item(1).Range('B2').Text; a3 = $csv.Worksheets.Item(1).Range('A3').Text; a3Formula = $csv.Worksheets.Item(1).Range('A3').HasFormula }
  $csv.Close($false); $book.Close($false)
} finally { if (-not $wasRunning -and $excel.Workbooks.Count -eq 0) { $excel.Quit() }; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($excel) }

$wasRunning = Running 'POWERPNT'
$ppt = New-Object -ComObject PowerPoint.Application
try {
  # ReadOnly=-1 (msoTrue), Untitled=0, WithWindow=0: no window appears.
  $deck = $ppt.Presentations.Open((Join-Path $Folder 'deck.pptx'), -1, 0, 0)
  $slides = @(); foreach ($s in $deck.Slides) { $texts = @(); $pictures = 0; foreach ($sh in $s.Shapes) { if ($sh.HasTextFrame) { $texts += $sh.TextFrame.TextRange.Text.Replace("`r", ' / ') }; if ($sh.Type -eq 13) { $pictures++ } }; $slides += [ordered]@{ texts = $texts; pictures = $pictures } }
  $results.powerpoint = [ordered]@{ count = $deck.Slides.Count; width = $deck.PageSetup.SlideWidth; height = $deck.PageSetup.SlideHeight; slides = $slides }
  $deck.Close()
} finally { if (-not $wasRunning -and $ppt.Presentations.Count -eq 0) { $ppt.Quit() }; [void][Runtime.InteropServices.Marshal]::ReleaseComObject($ppt) }

$results | ConvertTo-Json -Depth 6
