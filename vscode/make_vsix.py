#!/usr/bin/env python3
"""Packs the extension into a .vsix without Node or vsce.

usage: make_vsix.py SOURCE_DIR VERSION OUTPUT.vsix
A VSIX is a zip with [Content_Types].xml, extension.vsixmanifest and the
extension files under extension/.
"""
import json
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

FILES = ["extension.js", "lib.js", "package.nls.json", "package.nls.ru.json", "README.md"]


def main():
    source, version, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    package = json.loads((source / "package.json").read_text(encoding="utf-8"))
    package["version"] = version
    nls = json.loads((source / "package.nls.json").read_text(encoding="utf-8"))
    description = nls.get("description", "")
    manifest = f"""<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="{escape(package['name'])}" Version="{escape(version)}" Publisher="{escape(package['publisher'])}" />
    <DisplayName>{escape(package['displayName'])}</DisplayName>
    <Description xml:space="preserve">{escape(description)}</Description>
    <Categories>{escape(','.join(package.get('categories', [])))}</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{escape(package['engines']['vscode'])}" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"""
    content_types = """<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json" />
  <Default Extension=".js" ContentType="application/javascript" />
  <Default Extension=".md" ContentType="text/markdown" />
  <Default Extension=".vsixmanifest" ContentType="text/xml" />
</Types>
"""
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("extension.vsixmanifest", manifest)
        z.writestr("extension/package.json", json.dumps(package, indent=2, ensure_ascii=False))
        for name in FILES:
            z.write(source / name, "extension/" + name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
