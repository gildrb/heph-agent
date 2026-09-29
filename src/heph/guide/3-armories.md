# Your own armories

## How do I make my own armory?

Pick one:

- In a terminal, go to a folder of your files and run `heph init`, then `heph`.
- Run `heph init notes` to make an empty armory called notes in `~/.armories`.
- Inside Heph, type `/armory` and pick "use this folder" or "new armory".

Heph refuses to turn your home folder or `/` into an armory.

## How do I add my own files?

Type `/add` and paste or drag a file or folder, or type `/add ~/Documents/report.pdf`.
Heph copies it into the armory; it never overwrites a file that is already there.
You can also copy files into the armory folder with any file manager.
Heph notices new, changed and deleted files before your next question and re-indexes them.

## How do I switch armories?

Type `/armory` and pick one from the list, or type `/armory notes`. Names forgive typos.
From a terminal, `heph notes` opens the armory called notes, and `heph` inside an armory
folder opens that folder. Outside any armory, `heph` opens this guide.

## Which files can Heph read?

Text of any kind (Markdown, plain text, code, CSV, HTML), PDF, DOCX, PPTX, XLSX, ODT
and ODS. Old .doc, .ppt, .xls, .odp and .rtf files are skipped: save them as PDF or
DOCX first. Files over 50 MB, text over 5 MB, scanned PDFs without text and binary
files are skipped, and the reason is shown when indexing.

## How do I keep files out of the index?

Add a `.harnessignore` file to the armory root with one pattern per line, like
`.gitignore`: `drafts/`, `*.log`, `old/*.pdf`. `.git`, `node_modules` and
`__pycache__` are always ignored.
