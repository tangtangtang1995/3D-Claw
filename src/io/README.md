# IO Layer

This directory owns file import/export and format conversion.

It should not render UI, decide algorithm policy, own worker lifecycle, or call
into dialogs/windows. UI code may call services or IO helpers to load/save data,
but file parsing and serialization live here rather than under `src/app`.
