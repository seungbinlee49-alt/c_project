# Third-party notices

DMD CPU64 INT8 uses the DMD research method/model and ONNX Runtime; see [algorithm-dmd/NOTICE.txt](algorithm-dmd/NOTICE.txt) and its bundled upstream licenses. The original FingerJet source remains replaceable and available in `third_party/FingerJetFXOSE-master`.

## Preserved V1 notices


This product includes the DigitalPersona FingerJetFX OSE fingerprint feature extractor. (http://digitalpersona.com/fingerjetfx)

Only the feature-extraction library is used. Our code implements the local descriptor, geometric-consensus matching, KISA adapters, evaluation engine, reports and Windows GUI. We do not execute the upstream sample application or use a third-party standalone matcher.

Upstream: https://github.com/FingerJetFXOSE/FingerJetFXOSE

The unmodified upstream source is retained under `third_party`. The original downloaded source archive is kept locally and excluded from Git to avoid duplicating the source tree. The upstream code is licensed under LGPL-3.0-or-later subject to its COPYRIGHT.txt conditions; the original COPYRIGHT.txt, COPYING.txt and COPYING.LESSER.txt accompany the source and distribution. See those files for the exact terms. The extractor is built as a separate replaceable FRFXLL.dll. This product does not claim endorsement or certification from DigitalPersona, HID Global, KISA or NIST.

The repository's statement about the initial extractor's historical MINEX results does not certify our matcher, this Windows build or the complete project.

The matcher uses local relative minutia neighborhoods and geometric consensus. The local-neighborhood representation was informed by the fingerprint-matching literature, including R. Cappelli, M. Ferrara and D. Maltoni, “Minutia Cylinder-Code: a new representation and matching technique for fingerprint recognition,” IEEE TPAMI 32(12), 2010, DOI: 10.1109/TPAMI.2010.52. It is not presented as an implementation of that algorithm or as reproducing its reported accuracy.
