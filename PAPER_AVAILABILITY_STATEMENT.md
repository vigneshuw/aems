# Availability statement — text for the manuscript

The current preprint states the hardware and firmware are *"released openly to support
reproducibility and extension,"* but the manuscript contains no actual link to the repository.
Add one of the paragraphs below (whichever matches your venue's section conventions) so readers
can find the release. For an arXiv preprint, Option A or C as a short "Code and Data
Availability" section near the end works well.

---

## Option A — "Code and Hardware Availability" section

> **Code and Hardware Availability.** The complete AEMS design is released as open source. The
> hardware (KiCad schematics, PCB layout, bill of materials, and manufacturing files), the
> STM32H745 dual-core firmware, and the host-side Python control software are available at
> https://github.com/vigneshuw/aems. A versioned, citable archive of the release is deposited on
> Zenodo (DOI: _to be added on release_).

## Option B — one-line footnote (for the introduction/abstract)

> The open-source hardware, firmware, and software are available at
> https://github.com/vigneshuw/aems.

## Option C — end-of-paper "Data and Code Availability" (journal-style)

> **Data and Code Availability.** The hardware design files, firmware, and host software
> supporting this study are openly available in the AEMS repository at
> https://github.com/vigneshuw/aems, archived at Zenodo (DOI: _to be added_). No restrictions
> apply to reuse beyond the terms of the released license(s).

---

## Before submission — checklist

- [ ] Insert one of the statements above so the "released openly" claim resolves to a real link.
- [x] Keywords finalized (Energy Monitoring, Open-source hardware, Condition Monitoring,
      Industry 4.0, Data acquisition).
- [x] Broken figure references (`Fig. ??`) resolved.
- [ ] Confirm the GitHub repo is **public** before the preprint is posted.
- [ ] Create a Zenodo archive (link the GitHub repo, cut a tagged release) and paste the DOI into
      this statement, `README.md`, and `CITATION.cff`.
- [ ] Confirm IJPEM-ST's preprint policy and disclose the arXiv preprint at submission if posted.
- [ ] Ensure the repo `LICENSE` is finalized (or clearly marked) before the repo goes public.
