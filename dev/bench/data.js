window.BENCHMARK_DATA = {
  "lastUpdate": 1793279978298,
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
          "id": "2f87c420c0f0bee3e9fdf59e2afa1203b1024e36",
          "message": "Merge pull request #6 from Zanders214/claude/hide-osc2\n\nKeep OSC 2 out of sight until it is added",
          "timestamp": "2026-10-29T11:49:25+01:00",
          "tree_id": "f2c43e1311d3788141049599d8c45a2100acc595",
          "url": "https://github.com/Zanders214/Synth2/commit/2f87c420c0f0bee3e9fdf59e2afa1203b1024e36"
        },
        "date": 1793271067089,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 133609.776,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.253,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 577149.098,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 5.411,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 891409.166,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 8.357,
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
          "id": "8c892a8d80a0dfe746b8bb2255ebbf6128dc51a7",
          "message": "Merge pull request #7 from Zanders214/claude/faster-dsp\n\nPlay each note's oscillators side by side, about twice as fast, to the same samples bit for bit",
          "timestamp": "2026-10-29T12:19:28+01:00",
          "tree_id": "fd8eea02ca0eff3162cb7bfcc374e7856d324430",
          "url": "https://github.com/Zanders214/Synth2/commit/8c892a8d80a0dfe746b8bb2255ebbf6128dc51a7"
        },
        "date": 1793272890156,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 102992.063,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.966,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 344151.922,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.226,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 646849.286,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 6.064,
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
          "id": "9238415c9b487011b499c4170f565d585c1f4863",
          "message": "Merge pull request #8 from Zanders214/claude/sonar-copy-struct\n\nClear the SonarCloud findings: one struct per unison copy, render in steps, callbacks behind setters",
          "timestamp": "2026-10-29T12:37:18+01:00",
          "tree_id": "2464bcdf91749d4c45871d8dfdc4c5c5bcee922e",
          "url": "https://github.com/Zanders214/Synth2/commit/9238415c9b487011b499c4170f565d585c1f4863"
        },
        "date": 1793273964367,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 116949.544,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.096,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 397825.455,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.73,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 741809.142,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 6.954,
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
          "id": "940c47ec851d3ddfd48f7302efeeb7d38d07e3aa",
          "message": "Merge pull request #9 from Zanders214/claude/bench-engine\n\nReady the engine for the bench: four oscillators, unison and envelope that can be switched off, routing to the output",
          "timestamp": "2026-10-29T14:12:16+01:00",
          "tree_id": "c4c61bf1ffcdbf37c191f17b60e8a47cf4c42ad1",
          "url": "https://github.com/Zanders214/Synth2/commit/940c47ec851d3ddfd48f7302efeeb7d38d07e3aa"
        },
        "date": 1793279656192,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 76632.743,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.718,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 311738.65,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 2.923,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 627691.038,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 5.885,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1162965.142,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 10.903,
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
          "id": "9a0a6683d9232322f6027b1a1639950c59e01f45",
          "message": "Merge pull request #10 from Zanders214/claude/bench-ui\n\nTurn the editor into a workbench: an empty bench to add modules to and arrange freely",
          "timestamp": "2026-10-29T14:17:16+01:00",
          "tree_id": "4843da44bd45661876b01b4df6a1e63384c547c8",
          "url": "https://github.com/Zanders214/Synth2/commit/9a0a6683d9232322f6027b1a1639950c59e01f45"
        },
        "date": 1793279977875,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 98247,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.921,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 403752.326,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.785,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 761719.919,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.141,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1423848.55,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 13.349,
            "unit": "%"
          }
        ]
      }
    ]
  }
}