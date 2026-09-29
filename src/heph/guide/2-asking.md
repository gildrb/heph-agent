# Asking questions

## How do I ask a question?

Type the question and press Enter. There is no command for it. Follow-up questions
remember the last few turns of the chat, so "and in 2023?" works. `/new` starts a fresh
chat. Ctrl-C stops an answer that is still being written. Ctrl-D or `/exit` quits.

## What do the citations mean?

Each answer cites evidence as [E1], [E2] and so on. Every citation carries a short quote,
and Heph checks that quote against the passage it names:

- ✓ verified: the quote is really in that passage.
- ✗ not found: the quote is not in the passage, or the passage does not exist.
- ? no quote: the model cited a passage without quoting it.

An answer made only of ✓ citations is backed by your files. Treat ✗ and ? as claims to check.

## How do I see the passages behind an answer?

Type `/sources`. It lists each passage Heph sent to the model for the last answer, with
its file, page and the start of its text.

## Why did Heph say it could not find the answer?

Heph only sends the model the passages that best match the words of your question.
Use the words your files use, name the document or topic, or ask a narrower question.
If the file is new, it is indexed on your next question; check it was not skipped.
