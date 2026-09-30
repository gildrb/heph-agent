# Asking questions

## How do I ask a question?

Type the question and press Enter. There is no command for it. Alt+Enter starts a new
line. Follow-up questions remember the last few turns of the chat, so "and in 2023?"
works. `/new` starts a fresh chat. Esc stops an answer that is still being written, and
what you typed meanwhile stays in the prompt. Ctrl+D or `/exit` quits.

## What are the lines around an answer?

- "Read 5 passages" names the files Heph found for the question and sent to the model.
- "Thought for 6.2s" is how long the model took before it started answering. Ctrl+T
  shows or hides everything it thought, for the next answers.
- Under the answer, one line per cited passage: ✓ when its quotes are really in it.
- The last line is the model, tokens in and out, the time and the speed.

## What do the citations mean?

Each answer cites evidence as [E1], [E2] and so on. Every citation carries a short quote,
and Heph checks that quote against the passage it names:

- ✓ verified: the quote is really in that passage.
- ✗ not found: the quote is not in the passage, or the passage does not exist.
- ? no quote: the model cited a passage without quoting it.

An answer made only of ✓ citations is backed by your files. Treat ✗ and ? as claims to check.

## How do I see the passages behind an answer?

Press Ctrl+O, or type `/sources`. It shows each passage Heph sent to the model for the
last answer, in full, with its file and page, and marks the ones the answer cited.

## Why did Heph say it could not find the answer?

Heph only sends the model the passages that best match the words of your question.
Use the words your files use, name the document or topic, or ask a narrower question.
If the file is new, it is indexed on your next question; check it was not skipped.
