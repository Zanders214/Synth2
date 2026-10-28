window.BENCHMARK_DATA = {
  "lastUpdate": 1793192440420,
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
      }
    ]
  }
}