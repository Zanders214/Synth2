window.BENCHMARK_DATA = {
  "lastUpdate": 1793550090512,
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
          "id": "4e2a31dacfbea4b210de9d64758ed4be91db07cd",
          "message": "Merge pull request #11 from Zanders214/claude/osc-unison-envelopes\n\nGive each oscillator its own unison stack and its own envelope",
          "timestamp": "2026-10-29T15:34:50+01:00",
          "tree_id": "fd83071278ba56a2a16b371bb7779a989a3c6d15",
          "url": "https://github.com/Zanders214/Synth2/commit/4e2a31dacfbea4b210de9d64758ed4be91db07cd"
        },
        "date": 1793284627001,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 112044.394,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.05,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 413277.511,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.874,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 822487.184,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.711,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1549335.059,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.525,
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
          "id": "1fe6d379721f9eb4f7aa082c806c8ad562f072ec",
          "message": "Merge pull request #13 from Zanders214/claude/dahdsr-envelopes\n\nGive each envelope delay, hold, decay and sustain stages",
          "timestamp": "2026-10-29T23:34:51+01:00",
          "tree_id": "00c9a0e0c58ae608893fc7d425fac5a56ead827a",
          "url": "https://github.com/Zanders214/Synth2/commit/1fe6d379721f9eb4f7aa082c806c8ad562f072ec"
        },
        "date": 1793313431754,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 98134.523,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.92,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 397762.876,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.729,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 830821.195,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.789,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1608293.183,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 15.078,
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
          "id": "28b207c1676e572897c11d3c0b5681b422ca126d",
          "message": "Merge pull request #12 from Zanders214/claude/factory-presets\n\nAdd factory presets: a picker in the header, and the host's program list",
          "timestamp": "2026-10-29T23:45:51+01:00",
          "tree_id": "51fb513377b1051c60aa7a0c5f4d23e2296617e9",
          "url": "https://github.com/Zanders214/Synth2/commit/28b207c1676e572897c11d3c0b5681b422ca126d"
        },
        "date": 1793314095455,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 102433.592,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.96,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 393308.735,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.687,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 822916.688,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.715,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1607285.576,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 15.068,
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
          "id": "b9547af3e15c9a48b52c8174e5c6e0996112f66c",
          "message": "Merge pull request #14 from Zanders214/claude/effects\n\nAdd an Effects tab with a delay and a reverb after the mix",
          "timestamp": "2026-10-30T01:18:29+01:00",
          "tree_id": "ef1001ad6a8648853a8629fd9def0fe4be22594f",
          "url": "https://github.com/Zanders214/Synth2/commit/b9547af3e15c9a48b52c8174e5c6e0996112f66c"
        },
        "date": 1793319645091,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 95328.563,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.894,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 375685.946,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.522,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 760406.214,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.129,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1491321.229,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 13.981,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1634186.959,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 15.321,
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
          "id": "13c7b95f85d1f563ce672625f2fae70d4e184083",
          "message": "Merge pull request #15 from Zanders214/claude/bypass\n\nPower switches: bypass any oscillator, envelope or effect",
          "timestamp": "2026-10-30T01:56:34+01:00",
          "tree_id": "b8006a59b0635e770b39d40be00e1a2e4ab69fa4",
          "url": "https://github.com/Zanders214/Synth2/commit/13c7b95f85d1f563ce672625f2fae70d4e184083"
        },
        "date": 1793321891881,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 64565.913,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.605,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 263522.944,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 2.471,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 525464.771,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 4.926,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1041687.977,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 9.766,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1123236.981,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 10.53,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Dennis Zanders",
            "username": "Zanders214",
            "email": "152227414+Zanders214@users.noreply.github.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "13c7b95f85d1f563ce672625f2fae70d4e184083",
          "message": "Merge pull request #15 from Zanders214/claude/bypass\n\nPower switches: bypass any oscillator, envelope or effect",
          "timestamp": "2026-10-30T00:56:34Z",
          "url": "https://github.com/Zanders214/Synth2/commit/13c7b95f85d1f563ce672625f2fae70d4e184083"
        },
        "date": 1793323341529,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 104584.612,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.98,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 403852.914,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.786,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 830004.901,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.781,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1611176.477,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 15.105,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1759629.259,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.497,
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
          "id": "fccc2f1a824c1d8093bd841179832728acdf3bfe",
          "message": "Merge pull request #16 from Zanders214/claude/envelope-editing\n\nDrag the envelope graph; Knobs / Graph / Both and Full / Compact views",
          "timestamp": "2026-10-30T02:32:49+01:00",
          "tree_id": "6c2382eb5f415836beb7222818907671a7f162c3",
          "url": "https://github.com/Zanders214/Synth2/commit/fccc2f1a824c1d8093bd841179832728acdf3bfe"
        },
        "date": 1793324073973,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 69526.243,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.652,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 280364.189,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 2.628,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 554942.147,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 5.203,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1099834.067,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 10.311,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1185616.576,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 11.115,
            "unit": "%"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "Dennis Zanders",
            "username": "Zanders214",
            "email": "152227414+Zanders214@users.noreply.github.com"
          },
          "committer": {
            "name": "GitHub",
            "username": "web-flow",
            "email": "noreply@github.com"
          },
          "id": "fccc2f1a824c1d8093bd841179832728acdf3bfe",
          "message": "Merge pull request #16 from Zanders214/claude/envelope-editing\n\nDrag the envelope graph; Knobs / Graph / Both and Full / Compact views",
          "timestamp": "2026-10-30T01:32:49Z",
          "url": "https://github.com/Zanders214/Synth2/commit/fccc2f1a824c1d8093bd841179832728acdf3bfe"
        },
        "date": 1793370603367,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 103914.771,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.974,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 401746.465,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.766,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 822510.224,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.711,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1604244.962,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 15.04,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1753024.427,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.435,
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
          "id": "c003fb4e6913b6d65ab28a307c70783549f91894",
          "message": "Merge pull request #17 from Zanders214/claude/drive-and-keys\n\nAdd a DRIVE effect, and play the standalone app from MIDI keyboards and the computer's keys",
          "timestamp": "2026-10-30T15:38:13+01:00",
          "tree_id": "968e89d417873be544ba3e611bb42d62db5a0460",
          "url": "https://github.com/Zanders214/Synth2/commit/c003fb4e6913b6d65ab28a307c70783549f91894"
        },
        "date": 1793371259693,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 109147.19,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 1.023,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 440964.231,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 4.134,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 920051.137,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 8.625,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1781156.131,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 16.698,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1967219.603,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 18.443,
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
          "id": "6eeef3ea6a9aa204e7ded0304909484e755dce5f",
          "message": "Merge pull request #18 from Zanders214/claude/effects-rack\n\nLay the EFFECTS tab out like the bench: add, move, remove and reorder effects",
          "timestamp": "2026-10-30T16:12:47+01:00",
          "tree_id": "1ef8f9ca173ab6e82d376c27722b64f79f47fc11",
          "url": "https://github.com/Zanders214/Synth2/commit/6eeef3ea6a9aa204e7ded0304909484e755dce5f"
        },
        "date": 1793373321338,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 104702.463,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.982,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 404686.059,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.794,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 829318.039,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.775,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1584743.778,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.857,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1807702.394,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.947,
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
          "id": "bece65f8af6ee8448f47f9e05f47fa4a952b5aea",
          "message": "Merge pull request #19 from Zanders214/claude/sonar-fixes\n\nClear SonarCloud's open issues: the float-counter bugs and 95 code smells",
          "timestamp": "2026-10-30T17:06:43+01:00",
          "tree_id": "e7fa40e9a39a42df0b981e7d5596b51ccb4b2e9a",
          "url": "https://github.com/Zanders214/Synth2/commit/bece65f8af6ee8448f47f9e05f47fa4a952b5aea"
        },
        "date": 1793376562775,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 102462.615,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.961,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 392149.059,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.676,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 806128.917,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.557,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1573453.917,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.751,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1751399.628,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.419,
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
          "id": "d536ca7321878d70b1b458a463156423d2e0181a",
          "message": "Merge pull request #20 from Zanders214/claude/class-split\n\nSplit the four classes SonarCloud flags for too many methods (cpp:S1448)",
          "timestamp": "2026-10-30T17:12:21+01:00",
          "tree_id": "8561f7295e8ec0356b3282060ebcc70b5433e6ca",
          "url": "https://github.com/Zanders214/Synth2/commit/d536ca7321878d70b1b458a463156423d2e0181a"
        },
        "date": 1793376885928,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 102822.949,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.964,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 399347.762,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.744,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 816354.117,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.653,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1587359.293,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.881,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1779709.549,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.685,
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
          "id": "a2380b0b5552e2ed91b8502aa0bac098f83958b1",
          "message": "Merge pull request #21 from Zanders214/claude/envelope-fill\n\nFit the AMP envelope to its graph so it looks the same at every size",
          "timestamp": "2026-10-30T18:20:55+01:00",
          "tree_id": "4db36784f9be91ab08c4819f1ed4eed4984a0f82",
          "url": "https://github.com/Zanders214/Synth2/commit/a2380b0b5552e2ed91b8502aa0bac098f83958b1"
        },
        "date": 1793381016271,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 99956.526,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.937,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 393625.99,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.69,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 815839.325,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.648,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1585236.581,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.862,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1761037.361,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 16.51,
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
          "id": "bc4cbbcc65b04de314d338a225d1210afb23f9d8",
          "message": "Merge pull request #22 from Zanders214/claude/envelope-curves\n\nBend the envelope's ramps with dots on the AMP graph, as in Vital",
          "timestamp": "2026-10-31T17:02:12+01:00",
          "tree_id": "545c9346abbc04193705989fffcaaaa72931687c",
          "url": "https://github.com/Zanders214/Synth2/commit/bc4cbbcc65b04de314d338a225d1210afb23f9d8"
        },
        "date": 1793462674125,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 93167.642,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.873,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 376159.439,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.526,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 781860.114,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.33,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1497872.856,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 14.043,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1687024.709,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 15.816,
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
          "id": "1977a110d935a7b6b2f6293b13c3ce6c113dd41c",
          "message": "Merge pull request #23 from Zanders214/claude/bench-zoom\n\nZoom and pan the bench, with room for twelve cards",
          "timestamp": "2026-10-31T17:56:36+01:00",
          "tree_id": "eb60ccb961406d47f0ee226a50ba2aa54cbc3588",
          "url": "https://github.com/Zanders214/Synth2/commit/1977a110d935a7b6b2f6293b13c3ce6c113dd41c"
        },
        "date": 1793465954606,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 93005.411,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.872,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 374410.396,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 3.51,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 772698.939,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 7.244,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1487032.953,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 13.941,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1679538.209,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 15.746,
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
          "id": "5ec5ef05c9656f71756309d2bad0bfb5662e8641",
          "message": "Merge pull request #24 from Zanders214/claude/filter",
          "timestamp": "2026-10-31T18:30:07+01:00",
          "tree_id": "495e3d523ee48f5a2a253551b4a859f61bc86301",
          "url": "https://github.com/Zanders214/Synth2/commit/5ec5ef05c9656f71756309d2bad0bfb5662e8641"
        },
        "date": 1793467919455,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 65827.656,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.617,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 267799.511,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 2.511,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 535600.623,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 5.021,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1052303.307,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 9.865,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1149941.624,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 10.781,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + filters",
            "value": 1409299.397,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + filters @48k/512",
            "value": 13.212,
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
          "id": "faf41f8781d94d948b356adb780df5e812335401",
          "message": "Merge pull request #26 from Zanders214/claude/perf-gate\n\nGate the benchmark on head vs base on one runner, not on stored numbers",
          "timestamp": "2026-11-01T17:18:58+01:00",
          "tree_id": "34e013d5a93dbc62d61454d6c58041fd0498838f",
          "url": "https://github.com/Zanders214/Synth2/commit/faf41f8781d94d948b356adb780df5e812335401"
        },
        "date": 1793550089980,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "processBlock 8x7",
            "value": 60576.797,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 8x7 @48k/512",
            "value": 0.568,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16",
            "value": 257851.642,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 @48k/512",
            "value": 2.417,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 2-osc",
            "value": 514010.384,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 2-osc @48k/512",
            "value": 4.819,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc",
            "value": 1022464.11,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc @48k/512",
            "value": 9.586,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + effects",
            "value": 1129559.365,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + effects @48k/512",
            "value": 10.59,
            "unit": "%"
          },
          {
            "name": "processBlock 16x16 4-osc + filters",
            "value": 1356862.117,
            "unit": "ns/block"
          },
          {
            "name": "DSP load 16x16 4-osc + filters @48k/512",
            "value": 12.721,
            "unit": "%"
          }
        ]
      }
    ]
  }
}