# Pluviora example

This Android/iOS example selects and previews a supported special-file bundle.

```bash
flutter pub get
flutter run
```

The UI accepts a JSON document and audio file, plus optional same-stem static
ordering hints and a background image. Selecting several files together pairs
them by filename stem; each choice can be replaced manually. The ordering file
is scanned as text and never executed.

`file_picker` is used only by the example. The core package accepts paths or
bytes and does not depend on a file picker.
