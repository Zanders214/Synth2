window.BENCHMARK_DATA = {
  "lastUpdate": 1793269058308,
  "repoUrl": "https://github.com/Zanders214/Synth2",
  "entries": {
    "ZandersSynth2 DSP": [
      {
        "commit": {
          "author": {
            "email": "152227414+Zanders214@users.noreply.github.com",
            "name": "Dennis Zanders",
            "username": "Zanders214"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f7e07ecef57b1eb0488e6ddb579176ff2f7d24d0",
          "message": "Merge pull request #1 from Zanders214/claude/basic-synth\n\nAdd a polyphonic synth: four wave shapes, unison voices and detune",
          "timestamp": "2026-10-28T13:57:30+01:00",
          "tree_id": "032ae2b4f3440e2778020321dd12d5edecf8c6fd",
          "url": "https://github.com/Zanders214/Synth2/commit/f7e07ecef57b1eb0488e6ddb579176ff2f7d24d0"
        },
        "date": 1793192439785,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 180599.619,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.693,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 816081.261,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 7.651,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "152227414+Zanders214@users.noreply.github.com",
            "name": "Dennis Zanders",
            "username": "Zanders214"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "602ec2e01bdf2f68b4e9f55576eec14be57af534",
          "message": "Merge pull request #2 from Zanders214/claude/control-displays\n\nShow what the dials do: a UNISON well over its dials and an AMP envelope with live notes",
          "timestamp": "2026-10-28T14:59:51+01:00",
          "tree_id": "5cb24e1cbd7b7bde49840d852d4f0d4b9a2bb137",
          "url": "https://github.com/Zanders214/Synth2/commit/602ec2e01bdf2f68b4e9f55576eec14be57af534"
        },
        "date": 1793196068463,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 194452.224,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.823,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 869038.459,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 8.147,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "152227414+Zanders214@users.noreply.github.com",
            "name": "Dennis Zanders",
            "username": "Zanders214"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a3146773dbf7226afb999d1b36c8d953346bfdda",
          "message": "Merge pull request #3 from Zanders214/claude/sonar-cleanup\n\nClear the SonarCloud findings",
          "timestamp": "2026-10-29T10:36:22+01:00",
          "tree_id": "c72a4cf529d04487688b459919aef6a8f87d606a",
          "url": "https://github.com/Zanders214/Synth2/commit/a3146773dbf7226afb999d1b36c8d953346bfdda"
        },
        "date": 1793266715415,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 196502.865,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.842,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 876925.067,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 8.221,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "152227414+Zanders214@users.noreply.github.com",
            "name": "Dennis Zanders",
            "username": "Zanders214"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "3fec4825a25da0768a535b3d2844bd621513eb00",
          "message": "Merge pull request #4 from Zanders214/claude/wave-picker\n\nPick the wave in the WAVE well: a stepper pill, the scroll wheel and a drop-open tray of shapes",
          "timestamp": "2026-10-29T10:55:36+01:00",
          "tree_id": "648d81bca86594366f4466efa3bdffbbdb16bfd9",
          "url": "https://github.com/Zanders214/Synth2/commit/3fec4825a25da0768a535b3d2844bd621513eb00"
        },
        "date": 1793267844865,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 149190.107,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.399,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 664407,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 6.229,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "152227414+Zanders214@users.noreply.github.com",
            "name": "Dennis Zanders",
            "username": "Zanders214"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "77f69f27e4b921b3bd134659d9de2bb33d813683",
          "message": "Merge pull request #5 from Zanders214/claude/second-oscillator\n\nAdd a second oscillator: OSC 2 with its own shape, pitch, fine tune and level",
          "timestamp": "2026-10-29T11:15:36+01:00",
          "tree_id": "87057bb248edc08fd13c32c1a3be7626a69c0b84",
          "url": "https://github.com/Zanders214/Synth2/commit/77f69f27e4b921b3bd134659d9de2bb33d813683"
        },
        "date": 1793269057931,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 177593.289,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.665,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 793992.514,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 7.444,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 1235367.767,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 11.582,
            "unit": "%"
          }
        ]
      }
    ]
  }
}