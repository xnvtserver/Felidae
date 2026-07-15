import "str".

HtmlStyleSheet() =>
    base := StyleBase(),
    layout := StyleLayout(),
    controls := StyleControls(),
    code := StyleCode(),
    modal := StyleModal(),
    revamp := StyleRevamp(),
    part1 := str.concat(left: base, right: layout),
    part2 := str.concat(left: part1, right: controls),
    part3 := str.concat(left: part2, right: code),
    part4 := str.concat(left: part3, right: modal),
    return (str.concat(left: part4, right: revamp)).

CssRuleData(selector: string, declarations: string) =>
    return (cssRule(selector: selector, declarations: declarations)).

CssMediaData(query: string, rules: string) =>
    return (cssMedia(query: query, rules: rules)).

RenderCssRule(input: any) =>
    open := str.concat(left: input.selector, right: "{"),
    body := str.concat(left: open, right: input.declarations),
    return (str.concat(left: body, right: "}")).

RenderCssMedia(input: any) =>
    open := str.concat(left: "@media ", right: input.query),
    withBrace := str.concat(left: open, right: "{"),
    body := str.concat(left: withBrace, right: input.rules),
    return (str.concat(left: body, right: "}")).

StyleBase() =>
    root := RenderCssRule(input: CssRuleData(selector: ":root", declarations: "--brand-bg:#18f0d7;--brand-bg-2:#65fff1;--brand-soft:#0f3f3a;--brand-text:#031b19;--bg:#050809;--surface:#0d1417;--surface-2:#121e22;--text:#effffb;--muted:#a8bfbc;--border:#274347;--soft:#0d2d2c;--link:#65fff1;--header-bg:#061112;--header-text:#effffb;--nav-bg:#102d30;--nav-text:#dffdfa;--nav-active-bg:#18f0d7;--nav-active-text:#031b19;--code-bg:#092123;--code-bg-2:#0d3033;--code-text:#e9fffc;--shadow:0 16px 42px rgba(0,0,0,.42)")),
    light := RenderCssRule(input: CssRuleData(selector: "body.theme-light", declarations: "--brand-text:#053a35;--bg:#f3fffd;--surface:#ffffff;--surface-2:#eafffc;--text:#173033;--muted:#5d7374;--border:#a9ddda;--soft:#d9fbf7;--link:#087c72;--header-bg:#18f0d7;--header-text:#053a35;--nav-bg:rgba(8,124,114,.1);--nav-text:#173033;--nav-active-bg:#087c72;--nav-active-text:#ffffff;--code-bg:#f4fffd;--code-bg-2:#dff8f5;--code-text:#173033;--shadow:0 10px 24px rgba(14,84,80,.14)")),
    body := RenderCssRule(input: CssRuleData(selector: "body", declarations: "margin:0;font-family:Inter,Segoe UI,Arial,sans-serif;background:radial-gradient(circle at top left,rgba(24,240,215,.12),transparent 28%),var(--bg);color:var(--text);transition:background .18s ease,color .18s ease")),
    header := RenderCssRule(input: CssRuleData(selector: "header", declarations: "position:relative;background:linear-gradient(135deg,var(--header-bg),rgba(24,240,215,.18));color:var(--header-text);padding:18px 28px;box-shadow:var(--shadow);border-bottom:1px solid rgba(24,240,215,.26);z-index:2")),
    title := RenderCssRule(input: CssRuleData(selector: "header h1", declarations: "margin:0;font-size:22px;display:flex;align-items:center;gap:10px")),
    headerTop := RenderCssRule(input: CssRuleData(selector: ".header-top", declarations: "display:flex;align-items:flex-start;justify-content:space-between;gap:16px")),
    headerBrand := RenderCssRule(input: CssRuleData(selector: ".header-brand", declarations: "min-width:0")),
    mark := RenderCssRule(input: CssRuleData(selector: ".felidae-mark", declarations: "display:inline-grid;place-items:center;width:30px;height:30px;border-radius:8px;background:var(--brand-bg);color:var(--brand-text);font-weight:800;box-shadow:0 0 0 3px rgba(24,240,215,.18)")),
    subtitle := RenderCssRule(input: CssRuleData(selector: "header p", declarations: "margin:6px 0 0;color:var(--header-text)")),
    footer := RenderCssRule(input: CssRuleData(selector: "footer", declarations: "padding:26px;text-align:center;color:var(--muted)")),
    footerLink := RenderCssRule(input: CssRuleData(selector: "footer a", declarations: "color:var(--link)")),
    part1 := str.concat(left: root, right: light),
    part2 := str.concat(left: part1, right: body),
    part3 := str.concat(left: part2, right: header),
    part4 := str.concat(left: part3, right: title),
    part5 := str.concat(left: part4, right: headerTop),
    part5a := str.concat(left: part5, right: headerBrand),
    part6 := str.concat(left: part5a, right: mark),
    part7 := str.concat(left: part6, right: subtitle),
    part8 := str.concat(left: part7, right: footer),
    return (str.concat(left: part8, right: footerLink)).

StyleLayout() =>
    nav := RenderCssRule(input: CssRuleData(selector: "nav", declarations: "margin-top:12px;display:flex;gap:8px;align-items:center;overflow:auto;padding:2px 0 6px;scrollbar-width:thin")),
    navLink := RenderCssRule(input: CssRuleData(selector: "nav a", declarations: "flex:0 0 auto;color:var(--nav-text);text-decoration:none;font-size:14px;border:1px solid rgba(24,240,215,.26);padding:7px 10px;border-radius:6px;background:var(--nav-bg);white-space:nowrap;box-shadow:inset 0 1px 0 rgba(255,255,255,.04);display:inline-flex;align-items:center;gap:6px")),
    navActive := RenderCssRule(input: CssRuleData(selector: "nav a.active", declarations: "background:var(--nav-active-bg);color:var(--nav-active-text);border-color:var(--nav-active-bg);font-weight:700;box-shadow:0 8px 22px rgba(24,240,215,.22)")),
    navIcon := RenderCssRule(input: CssRuleData(selector: ".nav-icon", declarations: "display:inline-grid;place-items:center;min-width:22px;width:22px;height:22px;border-radius:6px;background:rgba(24,240,215,.16);color:var(--link);line-height:1")),
    iconSvg := RenderCssRule(input: CssRuleData(selector: ".icon-svg", declarations: "width:17px;height:17px;display:block;stroke:currentColor;fill:none;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round")),
    toolIconSvg := RenderCssRule(input: CssRuleData(selector: ".theme-toggle .icon-svg", declarations: "width:18px;height:18px")),
    moonHidden := RenderCssRule(input: CssRuleData(selector: ".icon-moon", declarations: "display:none")),
    sunLightHidden := RenderCssRule(input: CssRuleData(selector: "body.theme-light .icon-sun", declarations: "display:none")),
    moonLightVisible := RenderCssRule(input: CssRuleData(selector: "body.theme-light .icon-moon", declarations: "display:block")),
    navActiveIcon := RenderCssRule(input: CssRuleData(selector: "nav a.active .nav-icon", declarations: "background:rgba(3,27,25,.18);color:var(--nav-active-text)")),
    navLightIcon := RenderCssRule(input: CssRuleData(selector: "body.theme-light .nav-icon", declarations: "background:rgba(3,27,25,.1);color:#087c72")),
    navLightActiveIcon := RenderCssRule(input: CssRuleData(selector: "body.theme-light nav a.active .nav-icon", declarations: "background:rgba(255,255,255,.18);color:var(--nav-active-text)")),
    main := RenderCssRule(input: CssRuleData(selector: "main", declarations: "max-width:1120px;margin:0 auto;padding:28px")),
    section := RenderCssRule(input: CssRuleData(selector: "section", declarations: "background:linear-gradient(180deg,var(--surface),var(--surface-2));border:1px solid var(--border);border-radius:8px;padding:22px;margin:0 0 18px;box-shadow:0 1px 0 rgba(24,240,215,.1),0 12px 30px rgba(0,0,0,.12)")),
    route := RenderCssRule(input: CssRuleData(selector: ".route", declarations: "display:none")),
    routeActive := RenderCssRule(input: CssRuleData(selector: ".route.active", declarations: "display:block")),
    meta := RenderCssRule(input: CssRuleData(selector: ".route-meta", declarations: "font-size:13px;color:var(--muted);margin:-6px 0 14px")),
    actions := RenderCssRule(input: CssRuleData(selector: ".route-actions", declarations: "display:flex;gap:10px;flex-wrap:wrap;margin-top:18px")),
    h2 := RenderCssRule(input: CssRuleData(selector: "h2", declarations: "margin-top:0;color:var(--text)")),
    h3 := RenderCssRule(input: CssRuleData(selector: "h3", declarations: "margin-bottom:8px;color:var(--text)")),
    text := RenderCssRule(input: CssRuleData(selector: "p,li", declarations: "line-height:1.6")),
    grids := RenderCssRule(input: CssRuleData(selector: ".grid,.module-grid,.details-grid", declarations: "display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px")),
    card := RenderCssRule(input: CssRuleData(selector: ".mini-card", declarations: "border:1px solid var(--border);border-radius:8px;padding:14px;background:linear-gradient(180deg,var(--surface-2),var(--surface));box-shadow:inset 0 1px 0 rgba(255,255,255,.03)")),
    cardButton := RenderCssRule(input: CssRuleData(selector: ".mini-card .tool-button", declarations: "margin-top:8px")),
    note := RenderCssRule(input: CssRuleData(selector: ".note", declarations: "background:var(--soft);border-left:4px solid var(--brand-bg);padding:12px;margin:14px 0;color:var(--text)")),
    detail := RenderCssRule(input: CssRuleData(selector: ".doc-detail", declarations: "border:1px solid var(--border);border-radius:8px;background:var(--surface-2);margin:10px 0;padding:0")),
    detailSummary := RenderCssRule(input: CssRuleData(selector: ".doc-detail summary", declarations: "cursor:pointer;font-weight:700;padding:12px 14px;color:var(--link)")),
    detailText := RenderCssRule(input: CssRuleData(selector: ".doc-detail p", declarations: "margin:0;padding:0 14px 14px;color:var(--muted)")),
    timeline := RenderCssRule(input: CssRuleData(selector: ".timeline", declarations: "display:grid;gap:12px;margin:16px 0")),
    milestone := RenderCssRule(input: CssRuleData(selector: ".milestone", declarations: "display:grid;grid-template-columns:118px 1fr;gap:14px;align-items:start;border:1px solid var(--border);border-radius:8px;background:linear-gradient(180deg,var(--surface-2),var(--surface));padding:14px;box-shadow:inset 3px 0 0 var(--brand-bg)")),
    milestoneYear := RenderCssRule(input: CssRuleData(selector: ".milestone-year", declarations: "display:inline-flex;align-items:center;justify-content:center;min-height:34px;border-radius:6px;background:var(--brand-bg);color:var(--brand-text);font-weight:800;font-size:13px")),
    milestoneTitle := RenderCssRule(input: CssRuleData(selector: ".milestone-title", declarations: "margin:0 0 6px;color:var(--text);font-size:17px")),
    milestoneText := RenderCssRule(input: CssRuleData(selector: ".milestone-text", declarations: "margin:0;color:var(--muted);line-height:1.55")),
    mobileHeader := RenderCssRule(input: CssRuleData(selector: "header", declarations: "position:relative;padding:16px")),
    mobileTitle := RenderCssRule(input: CssRuleData(selector: "header h1", declarations: "font-size:20px")),
    mobileHeaderTop := RenderCssRule(input: CssRuleData(selector: ".header-top", declarations: "align-items:flex-start")),
    mobileMark := RenderCssRule(input: CssRuleData(selector: ".felidae-mark", declarations: "width:28px;height:28px")),
    mobileMain := RenderCssRule(input: CssRuleData(selector: "main", declarations: "padding:14px")),
    mobileSection := RenderCssRule(input: CssRuleData(selector: "section", declarations: "padding:16px")),
    mobileGrid := RenderCssRule(input: CssRuleData(selector: ".grid,.module-grid,.details-grid", declarations: "grid-template-columns:1fr")),
    mobileMilestone := RenderCssRule(input: CssRuleData(selector: ".milestone", declarations: "grid-template-columns:1fr;gap:10px")),
    mobileMilestoneYear := RenderCssRule(input: CssRuleData(selector: ".milestone-year", declarations: "justify-content:flex-start;padding:0 10px;width:max-content;max-width:100%")),
    mobileNav := RenderCssRule(input: CssRuleData(selector: "nav", declarations: "overflow:auto;flex-wrap:nowrap;padding-bottom:6px")),
    mobileNavLink := RenderCssRule(input: CssRuleData(selector: "nav a", declarations: "white-space:nowrap")),
    mobileActions := RenderCssRule(input: CssRuleData(selector: ".route-actions,.playground-tools", declarations: "gap:8px")),
    mobileCodeActions := RenderCssRule(input: CssRuleData(selector: ".code-actions", declarations: "justify-content:flex-start;margin:0 0 6px")),
    mobileCodeBox := RenderCssRule(input: CssRuleData(selector: ".code-box", declarations: "display:flex;flex-direction:column")),
    mobilePre := RenderCssRule(input: CssRuleData(selector: ".code-box pre", declarations: "padding:12px;max-height:360px")),
    mobileEditor := RenderCssRule(input: CssRuleData(selector: ".playground-editor", declarations: "min-height:220px")),
    mobile1 := str.concat(left: mobileHeader, right: mobileTitle),
    mobile2 := str.concat(left: mobile1, right: mobileMark),
    mobile2a := str.concat(left: mobile2, right: mobileHeaderTop),
    mobile3 := str.concat(left: mobile2a, right: mobileMain),
    mobile4 := str.concat(left: mobile3, right: mobileSection),
    mobile5 := str.concat(left: mobile4, right: mobileGrid),
    mobile5a := str.concat(left: mobile5, right: mobileMilestone),
    mobile5b := str.concat(left: mobile5a, right: mobileMilestoneYear),
    mobile6 := str.concat(left: mobile5b, right: mobileNav),
    mobile8 := str.concat(left: mobile6, right: mobileNavLink),
    mobile9a := str.concat(left: mobile8, right: mobileActions),
    mobile9 := str.concat(left: mobile9a, right: mobileCodeActions),
    mobile10 := str.concat(left: mobile9, right: mobileCodeBox),
    mobile11 := str.concat(left: mobile10, right: mobilePre),
    mobileRules := str.concat(left: mobile11, right: mobileEditor),
    mobile := RenderCssMedia(input: CssMediaData(query: "(max-width:720px)", rules: mobileRules)),
    part3a := str.concat(left: nav, right: navLink),
    part3 := str.concat(left: part3a, right: navActive),
    part3b := str.concat(left: part3, right: navIcon),
    part3svg := str.concat(left: part3b, right: iconSvg),
    part3tool := str.concat(left: part3svg, right: toolIconSvg),
    part3moon := str.concat(left: part3tool, right: moonHidden),
    part3sun := str.concat(left: part3moon, right: sunLightHidden),
    part3moon2 := str.concat(left: part3sun, right: moonLightVisible),
    part3c := str.concat(left: part3moon2, right: navActiveIcon),
    part3d := str.concat(left: part3c, right: navLightIcon),
    part3e := str.concat(left: part3d, right: navLightActiveIcon),
    part4a := str.concat(left: part3e, right: main),
    part4 := str.concat(left: part4a, right: section),
    part5 := str.concat(left: part4, right: route),
    part6 := str.concat(left: part5, right: routeActive),
    part7 := str.concat(left: part6, right: meta),
    part8 := str.concat(left: part7, right: actions),
    part9 := str.concat(left: part8, right: h2),
    part10 := str.concat(left: part9, right: h3),
    part11 := str.concat(left: part10, right: text),
    part12 := str.concat(left: part11, right: grids),
    part13 := str.concat(left: part12, right: card),
    part14 := str.concat(left: part13, right: cardButton),
    part15 := str.concat(left: part14, right: note),
    part16 := str.concat(left: part15, right: detail),
    part17 := str.concat(left: part16, right: detailSummary),
    part18 := str.concat(left: part17, right: detailText),
    part19 := str.concat(left: part18, right: timeline),
    part20 := str.concat(left: part19, right: milestone),
    part21 := str.concat(left: part20, right: milestoneYear),
    part22 := str.concat(left: part21, right: milestoneTitle),
    part23 := str.concat(left: part22, right: milestoneText),
    return (str.concat(left: part23, right: mobile)).

StyleControls() =>
    buttons := RenderCssRule(input: CssRuleData(selector: ".copy-code,.download-code,.tool-button,.theme-toggle", declarations: "border:1px solid var(--border);background:linear-gradient(180deg,var(--surface-2),var(--surface));color:var(--text);border-radius:6px;padding:6px 9px;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;gap:6px;box-shadow:inset 0 1px 0 rgba(255,255,255,.04)")),
    headerTools := RenderCssRule(input: CssRuleData(selector: ".header-tools", declarations: "display:flex;gap:8px;align-items:center;justify-content:flex-end;flex:0 0 auto")),
    themeToggle := RenderCssRule(input: CssRuleData(selector: ".theme-toggle", declarations: "width:36px;height:36px;justify-content:center;padding:0;border-color:rgba(24,240,215,.35);background:var(--nav-bg);color:var(--nav-text);font-size:16px")),
    lightButtons := RenderCssRule(input: CssRuleData(selector: "body.theme-light .copy-code,body.theme-light .download-code,body.theme-light .tool-button,body.theme-light .theme-toggle", declarations: "background:linear-gradient(180deg,#ffffff,#eafffc);color:#173033;border-color:#9ed8d4;box-shadow:0 1px 0 rgba(8,124,114,.08)")),
    codeActions := RenderCssRule(input: CssRuleData(selector: ".code-actions", declarations: "display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end;margin:0 0 6px")),
    done := RenderCssRule(input: CssRuleData(selector: ".copy-code.done,.download-code.done,.tool-button.done", declarations: "background:var(--brand-bg);color:var(--brand-text)")),
    downloadMark := RenderCssRule(input: CssRuleData(selector: ".download-mark", declarations: "display:inline-grid;place-items:center;width:38px;height:38px;border-radius:8px;background:var(--brand-bg);color:var(--brand-text);border:1px solid var(--brand-bg);font-weight:800;font-size:12px;letter-spacing:0;margin-bottom:8px")),
    playgroundTools := RenderCssRule(input: CssRuleData(selector: ".playground-tools", declarations: "display:flex;gap:10px;flex-wrap:wrap;margin:12px 0")),
    playgroundEditor := RenderCssRule(input: CssRuleData(selector: ".playground-editor", declarations: "width:100%;min-height:300px;box-sizing:border-box;border:1px solid var(--border);border-radius:8px;padding:14px;font-family:Cascadia Code,Consolas,monospace;font-size:13px;background:var(--code-bg);color:var(--code-text);resize:vertical;box-shadow:0 0 0 3px rgba(24,240,215,.04)")),
    searchPanel := RenderCssRule(input: CssRuleData(selector: ".search-panel", declarations: "display:none;background:linear-gradient(180deg,var(--surface),var(--surface-2));border:1px solid var(--border);border-radius:8px;padding:18px;margin:0 0 18px;box-shadow:0 10px 28px rgba(0,0,0,.12)")),
    searchPanelOpen := RenderCssRule(input: CssRuleData(selector: "body.search-open .search-panel", declarations: "display:block")),
    searchTitle := RenderCssRule(input: CssRuleData(selector: ".search-panel h2", declarations: "font-size:20px")),
    searchBox := RenderCssRule(input: CssRuleData(selector: ".search-box", declarations: "display:flex;gap:10px;align-items:center;flex-wrap:wrap")),
    searchInput := RenderCssRule(input: CssRuleData(selector: ".search-input", declarations: "flex:1 1 260px;min-width:0;border:1px solid var(--border);border-radius:6px;padding:10px 12px;font-size:15px;box-sizing:border-box;background:var(--surface-2);color:var(--text);outline-color:var(--brand-bg)")),
    searchPlaceholder := RenderCssRule(input: CssRuleData(selector: ".search-input::placeholder", declarations: "color:var(--muted)")),
    searchResults := RenderCssRule(input: CssRuleData(selector: ".search-results", declarations: "display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-top:12px")),
    searchResult := RenderCssRule(input: CssRuleData(selector: ".search-result", declarations: "display:block;border:1px solid var(--border);border-radius:8px;background:linear-gradient(180deg,var(--surface-2),var(--surface));padding:12px;color:var(--text);text-decoration:none")),
    searchResultTitle := RenderCssRule(input: CssRuleData(selector: ".search-result strong", declarations: "display:block;margin-bottom:5px;color:var(--link)")),
    searchEmpty := RenderCssRule(input: CssRuleData(selector: ".search-empty", declarations: "color:var(--muted);margin-top:10px")),
    part1 := str.concat(left: buttons, right: headerTools),
    part2a := str.concat(left: part1, right: themeToggle),
    part2b := str.concat(left: part2a, right: lightButtons),
    part2 := str.concat(left: part2b, right: codeActions),
    part3 := str.concat(left: part2, right: done),
    part4 := str.concat(left: part3, right: downloadMark),
    part5 := str.concat(left: part4, right: playgroundTools),
    part6 := str.concat(left: part5, right: playgroundEditor),
    part7 := str.concat(left: part6, right: searchPanel),
    part7a := str.concat(left: part7, right: searchPanelOpen),
    part8 := str.concat(left: part7a, right: searchTitle),
    part9 := str.concat(left: part8, right: searchBox),
    part10 := str.concat(left: part9, right: searchInput),
    part11 := str.concat(left: part10, right: searchPlaceholder),
    part12 := str.concat(left: part11, right: searchResults),
    part13 := str.concat(left: part12, right: searchResult),
    part14 := str.concat(left: part13, right: searchResultTitle),
    return (str.concat(left: part14, right: searchEmpty)).

StyleCode() =>
    box := RenderCssRule(input: CssRuleData(selector: ".code-box", declarations: "position:relative;margin:14px 0;max-width:100%")),
    pre := RenderCssRule(input: CssRuleData(selector: "pre", declarations: "overflow:auto;max-width:100%;max-height:420px;box-sizing:border-box;background:linear-gradient(180deg,var(--code-bg),var(--code-bg-2));color:var(--code-text);padding:14px 16px;margin:0;border-radius:8px;border:1px solid var(--border);box-shadow:inset 0 1px 0 rgba(24,240,215,.08)")),
    code := RenderCssRule(input: CssRuleData(selector: "code", declarations: "font-family:Cascadia Code,Consolas,monospace;font-size:13px;white-space:pre")),
    key := RenderCssRule(input: CssRuleData(selector: ".tok-key", declarations: "color:#18f0d7")),
    string := RenderCssRule(input: CssRuleData(selector: ".tok-str", declarations: "color:#fca5a5")),
    number := RenderCssRule(input: CssRuleData(selector: ".tok-num", declarations: "color:#fde68a")),
    fn := RenderCssRule(input: CssRuleData(selector: ".tok-fn", declarations: "color:#86efac")),
    comment := RenderCssRule(input: CssRuleData(selector: ".tok-comment", declarations: "color:#9ca3af;font-style:italic")),
    lightKey := RenderCssRule(input: CssRuleData(selector: "body.theme-light .tok-key", declarations: "color:#06756c")),
    lightString := RenderCssRule(input: CssRuleData(selector: "body.theme-light .tok-str", declarations: "color:#9f2f47")),
    lightNumber := RenderCssRule(input: CssRuleData(selector: "body.theme-light .tok-num", declarations: "color:#7c5a00")),
    lightFn := RenderCssRule(input: CssRuleData(selector: "body.theme-light .tok-fn", declarations: "color:#16703a")),
    lightComment := RenderCssRule(input: CssRuleData(selector: "body.theme-light .tok-comment", declarations: "color:#687d7e;font-style:italic")),
    part1 := str.concat(left: box, right: pre),
    part2 := str.concat(left: part1, right: code),
    part3 := str.concat(left: part2, right: key),
    part4 := str.concat(left: part3, right: string),
    part5 := str.concat(left: part4, right: number),
    part6 := str.concat(left: part5, right: fn),
    part7 := str.concat(left: part6, right: comment),
    part8 := str.concat(left: part7, right: lightKey),
    part9 := str.concat(left: part8, right: lightString),
    part10 := str.concat(left: part9, right: lightNumber),
    part11 := str.concat(left: part10, right: lightFn),
    return (str.concat(left: part11, right: lightComment)).

StyleModal() =>
    backdrop := RenderCssRule(input: CssRuleData(selector: ".modal-backdrop", declarations: "position:fixed;inset:0;background:rgba(0,0,0,.58);display:none;align-items:center;justify-content:center;padding:18px;z-index:5")),
    lightBackdrop := RenderCssRule(input: CssRuleData(selector: "body.theme-light .modal-backdrop", declarations: "background:rgba(6,40,38,.26)")),
    open := RenderCssRule(input: CssRuleData(selector: ".modal-backdrop.open", declarations: "display:flex")),
    modal := RenderCssRule(input: CssRuleData(selector: ".modal", declarations: "max-width:720px;width:100%;background:linear-gradient(180deg,var(--surface),var(--surface-2));border-radius:8px;border:1px solid var(--border);box-shadow:var(--shadow);padding:20px")),
    modalHeader := RenderCssRule(input: CssRuleData(selector: ".modal header", declarations: "position:static;background:transparent;color:var(--text);box-shadow:none;border-bottom:0;padding:0;display:flex;align-items:center;justify-content:space-between")),
    modalTitle := RenderCssRule(input: CssRuleData(selector: ".modal h3", declarations: "margin:0")),
    body := RenderCssRule(input: CssRuleData(selector: ".modal-body", declarations: "color:var(--muted)")),
    close := RenderCssRule(input: CssRuleData(selector: ".modal-close", declarations: "border:1px solid rgba(24,240,215,.35);background:var(--nav-bg);color:var(--nav-text);border-radius:6px;padding:6px 9px;cursor:pointer")),
    part0 := str.concat(left: backdrop, right: lightBackdrop),
    part1 := str.concat(left: part0, right: open),
    part2 := str.concat(left: part1, right: modal),
    part3 := str.concat(left: part2, right: modalHeader),
    part4 := str.concat(left: part3, right: modalTitle),
    part5 := str.concat(left: part4, right: body),
    return (str.concat(left: part5, right: close)).

StyleRevamp() =>
    return ("html{scroll-behavior:smooth}*{box-sizing:border-box}:root{--bg:#07090d;--surface:#10151b;--surface-2:#151d24;--surface-3:#1d2730;--text:#f4f7f5;--muted:#9ea9a6;--border:#2e3a42;--soft:#132722;--link:#5eead4;--brand-bg:#5eead4;--brand-bg-2:#b8f7ec;--brand-soft:#183c36;--brand-text:#061311;--header-bg:#07100f;--header-text:#f4f7f5;--nav-bg:rgba(255,255,255,.045);--nav-text:#d7e5e0;--nav-active-bg:#5eead4;--nav-active-text:#061311;--code-bg:#071316;--code-bg-2:#0e2024;--code-text:#e9fffb;--shadow:0 22px 70px rgba(0,0,0,.38);--radius:8px}body.theme-light{--bg:#f6f8f5;--surface:#ffffff;--surface-2:#edf4f0;--surface-3:#e1ece7;--text:#14201d;--muted:#64736e;--border:#c9d7d2;--soft:#e5f7f2;--link:#087a6f;--brand-bg:#0f9888;--brand-bg-2:#7cd7cc;--brand-soft:#d9f6f0;--brand-text:#ffffff;--header-bg:#f6f8f5;--header-text:#14201d;--nav-bg:rgba(8,122,111,.08);--nav-text:#243530;--nav-active-bg:#0f9888;--nav-active-text:#ffffff;--code-bg:#f8fffd;--code-bg-2:#e5f3ef;--code-text:#14201d;--shadow:0 18px 48px rgba(20,58,49,.12)}body{min-height:100vh;background:linear-gradient(180deg,rgba(94,234,212,.08),transparent 340px),linear-gradient(90deg,rgba(255,255,255,.035) 1px,transparent 1px),linear-gradient(180deg,rgba(255,255,255,.028) 1px,transparent 1px),var(--bg);background-size:auto,72px 72px,72px 72px;color:var(--text);letter-spacing:0}body:before{content:'';position:fixed;inset:0;pointer-events:none;background:radial-gradient(circle at 18% 12%,rgba(94,234,212,.18),transparent 30%),radial-gradient(circle at 88% 4%,rgba(184,247,236,.08),transparent 24%);z-index:-1}header{position:relative;padding:24px clamp(18px,4vw,52px) 18px;background:linear-gradient(180deg,rgba(7,16,15,.94),rgba(7,9,13,.72));border-bottom:1px solid rgba(94,234,212,.18);box-shadow:0 16px 48px rgba(0,0,0,.22);overflow:hidden}body.theme-light header{background:linear-gradient(180deg,rgba(246,248,245,.98),rgba(246,248,245,.8));border-bottom-color:rgba(15,152,136,.22)}header:after{content:'';position:absolute;left:0;right:0;bottom:0;height:1px;background:linear-gradient(90deg,transparent,var(--brand-bg),transparent);opacity:.7}.header-top{max-width:1380px;margin:0 auto;display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;align-items:start}.header-brand{display:grid;gap:18px}.brand-row{display:flex;align-items:center;gap:12px}.felidae-mark{width:42px;height:42px;border-radius:8px;background:linear-gradient(135deg,var(--brand-bg),var(--brand-bg-2));box-shadow:0 0 0 1px rgba(255,255,255,.14),0 14px 34px rgba(94,234,212,.18);font-size:14px;letter-spacing:0}.brand-title{margin:0;font-size:18px;line-height:1.1}.brand-kicker{margin:0 0 4px;color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.08em}.hero-panel{max-width:920px}.hero-panel h2{font-size:clamp(38px,6vw,76px);line-height:.95;margin:0;color:var(--header-text);letter-spacing:0}.hero-panel p{max-width:780px;margin:18px 0 0;color:var(--muted);font-size:clamp(16px,2vw,20px);line-height:1.65}.hero-actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:22px}.hero-link,.hero-command{min-height:42px;display:inline-flex;align-items:center;gap:8px;border-radius:8px;border:1px solid var(--border);padding:10px 14px;color:var(--text);text-decoration:none;background:rgba(255,255,255,.05)}.hero-link.primary{background:var(--brand-bg);color:var(--brand-text);border-color:var(--brand-bg);font-weight:800}.hero-command{font-family:Cascadia Code,Consolas,monospace;color:var(--link);max-width:100%;overflow:auto}.hero-stats{display:grid;grid-template-columns:repeat(3,minmax(120px,1fr));gap:10px;max-width:760px;margin-top:24px}.hero-stat{border:1px solid var(--border);background:rgba(255,255,255,.045);border-radius:8px;padding:12px}.hero-stat strong{display:block;font-size:20px;color:var(--brand-bg);line-height:1.1}.hero-stat span{display:block;margin-top:5px;color:var(--muted);font-size:12px;line-height:1.35}.header-tools{padding-top:4px}.theme-toggle{width:42px;height:42px;border-radius:8px;background:var(--nav-bg);border-color:var(--border)}nav{max-width:1380px;margin:22px auto 0;display:flex;gap:8px;padding:8px;border:1px solid var(--border);border-radius:8px;background:rgba(255,255,255,.04);box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}nav a{min-height:38px;border-radius:8px;border-color:transparent;background:transparent;padding:8px 11px;color:var(--nav-text)}nav a:hover{background:var(--nav-bg);color:var(--text)}nav a.active{background:var(--brand-bg);color:var(--brand-text);box-shadow:none}.nav-icon{border-radius:6px;background:rgba(94,234,212,.12)}main{max-width:1380px;padding:28px clamp(16px,4vw,52px);display:grid;grid-template-columns:minmax(0,1fr);gap:18px}section{border-radius:8px;border-color:var(--border);background:linear-gradient(180deg,rgba(255,255,255,.055),rgba(255,255,255,.025)),var(--surface);box-shadow:var(--shadow);padding:clamp(18px,3vw,34px);margin:0}.route.active{display:grid;grid-template-columns:minmax(0,1fr);gap:16px}.route h2{font-size:clamp(30px,4vw,52px);line-height:1;margin:0}.route-meta{margin:0;color:var(--link);font-family:Cascadia Code,Consolas,monospace}.route>p{max-width:920px;margin:0;color:var(--muted);font-size:16px;line-height:1.75}.section-extra{margin-top:2px}.grid,.module-grid,.details-grid{grid-template-columns:repeat(auto-fit,minmax(245px,1fr));gap:14px}.mini-card,.doc-detail,.milestone{border-radius:8px;border-color:var(--border);background:linear-gradient(180deg,var(--surface-2),var(--surface));box-shadow:inset 0 1px 0 rgba(255,255,255,.04)}.mini-card{padding:18px}.mini-card h3{font-size:16px;margin:0 0 8px}.mini-card p{margin:0;color:var(--muted)}.note{border:1px solid rgba(94,234,212,.24);border-left:4px solid var(--brand-bg);border-radius:8px;background:linear-gradient(180deg,var(--brand-soft),rgba(94,234,212,.05));padding:14px 16px;margin:0}.doc-detail{overflow:hidden}.doc-detail summary{padding:14px 16px;color:var(--text)}.doc-detail p{padding:0 16px 16px}.timeline{gap:14px}.milestone{box-shadow:inset 4px 0 0 var(--brand-bg);padding:16px}.milestone-year{border-radius:8px}.route-actions{position:sticky;bottom:16px;z-index:2;display:flex;justify-content:flex-end;gap:8px;padding:10px;border:1px solid var(--border);border-radius:8px;background:rgba(7,9,13,.78);backdrop-filter:blur(14px)}body.theme-light .route-actions{background:rgba(255,255,255,.82)}.tool-button,.copy-code,.download-code{min-height:38px;border-radius:8px;border-color:var(--border);background:var(--surface-2);color:var(--text);font-weight:700}.tool-button:hover,.copy-code:hover,.download-code:hover{border-color:var(--brand-bg);color:var(--link)}.code-box{margin:0;border:1px solid var(--border);border-radius:8px;overflow:hidden;background:var(--code-bg)}.code-actions{margin:0;padding:8px;border-bottom:1px solid var(--border);background:var(--surface-2)}pre{border:0;border-radius:0;background:linear-gradient(180deg,var(--code-bg),var(--code-bg-2));max-height:520px;padding:18px}.search-panel{position:sticky;top:12px;z-index:3;border-radius:8px;background:linear-gradient(180deg,var(--surface-2),var(--surface));box-shadow:var(--shadow);display:block}.search-panel h2{margin:0 0 8px}.search-box{margin-top:14px}.search-input{min-height:44px;border-radius:8px;background:var(--bg)}.search-result{border-radius:8px}.playground-editor{border-radius:8px;min-height:420px;font-size:14px;line-height:1.6}.modal{border-radius:8px}.modal-backdrop{backdrop-filter:blur(10px)}footer{max-width:1380px;margin:0 auto;padding:18px clamp(16px,4vw,52px) 36px;text-align:left}.footer-grid{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;align-items:center;border-top:1px solid var(--border);padding-top:18px}.footer-grid p{margin:0;color:var(--muted)}.footer-grid a{color:var(--link);text-decoration:none}@media (max-width:860px){.header-top{grid-template-columns:1fr}.header-tools{position:absolute;right:16px;top:16px}.hero-panel h2{padding-right:82px}.hero-stats{grid-template-columns:1fr}.hero-actions{flex-direction:column}.hero-link,.hero-command{width:100%;justify-content:center}nav{margin-top:18px}.nav-text{display:none}nav a{padding:8px}.route-actions{position:static;justify-content:stretch}.route-actions .tool-button{flex:1}.footer-grid{grid-template-columns:1fr}main{padding:18px 14px}section{padding:18px}.route h2{font-size:34px}.search-panel{position:relative;top:auto}.playground-editor{min-height:300px}}").

HtmlNavItem(label: "Overview", href: "#overview", order: 1).
HtmlNavItem(label: "Basics", href: "#basics", order: 2).
HtmlNavItem(label: "Start", href: "#start", order: 3).
HtmlNavItem(label: "Syntax", href: "#syntax", order: 4).
HtmlNavItem(label: "Facts DB", href: "#facts", order: 5).
HtmlNavItem(label: "Queries", href: "#queries", order: 6).
HtmlNavItem(label: "Methods", href: "#methods", order: 7).
HtmlNavItem(label: "Reference", href: "#reference", order: 8).
HtmlNavItem(label: "Stdlib", href: "#stdlib", order: 9).
HtmlNavItem(label: "Libraries", href: "#libraries", order: 10).
HtmlNavItem(label: "Probability", href: "#probability", order: 11).
HtmlNavItem(label: "Native", href: "#native", order: 12).
HtmlNavItem(label: "Debugging", href: "#debugging", order: 13).
HtmlNavItem(label: "Hosting", href: "#hosting", order: 14).
HtmlNavItem(label: "Server", href: "#server", order: 15).
HtmlNavItem(label: "Download", href: "#downloads", order: 16).
HtmlNavItem(label: "Version", href: "#version", order: 17).
HtmlNavItem(label: "Milestones", href: "#milestones", order: 18).
HtmlNavItem(label: "About", href: "#about", order: 19).
HtmlNavItem(label: "Playground", href: "#playground", order: 20).

HtmlSectionData(id: string, title: string, p: string, p2: string, code: string, note: string, code2: string) =>
    return (section(id: id, title: title, p: p, p2: p2, content: "", code: code, note: note, code2: code2)).

HtmlDivData(name: string, id: string, class: string, content: string) =>
    return (div(name: name, id: id, class: class, content: content)).

HtmlButtonData(label: string, id: string, class: string) =>
    return (button(label: label, id: id, class: class)).

HtmlIconButtonData(label: string, id: string, class: string, title: string) =>
    return (button(label: label, id: id, class: class, title: title)).

HtmlAnchorData(label: string, href: string, class: string) =>
    return (anchor(label: label, href: href, class: class)).

HtmlCardData(title: string, text: string) =>
    return (card(title: title, text: text)).

HtmlCardContentData(title: string, content: string) =>
    return (card(title: title, content: content)).

HtmlPlaygroundData(id: string, title: string, p: string, example: string, command: string, note: string) =>
    return (playground(id: id, title: title, p: p, example: example, command: command, note: note)).

HtmlRichSectionData(id: string, title: string, p: string, p2: string, content: string, code: string, note: string, code2: string) =>
    return (section(id: id, title: title, p: p, p2: p2, content: content, code: code, note: note, code2: code2)).

RenderHtmlTag(name: string, id: string, class: string, content: string) =>
    openName := str.concat(left: "<", right: name),
    withId := str.concat(left: openName, right: " id='"),
    idValue := str.concat(left: withId, right: id),
    withClass := str.concat(left: idValue, right: "' class='"),
    classValue := str.concat(left: withClass, right: class),
    openEnd := str.concat(left: classValue, right: "'>"),
    withContent := str.concat(left: openEnd, right: content),
    closeStart := str.concat(left: withContent, right: "</"),
    closeName := str.concat(left: closeStart, right: name),
    return (str.concat(left: closeName, right: ">")).

RenderDiv(input: any) =>
    return (RenderHtmlTag(name: input.name, id: input.id, class: input.class, content: input.content)).

RenderButton(input: any) =>
    return (RenderHtmlTag(name: "button", id: input.id, class: input.class, content: input.label)).

RenderIconButton(input: any) =>
    start := str.concat(left: "<button id='", right: input.id),
    withClass := str.concat(left: start, right: "' class='"),
    classValue := str.concat(left: withClass, right: input.class),
    withTitle := str.concat(left: classValue, right: "' title='"),
    titleValue := str.concat(left: withTitle, right: input.title),
    withAria := str.concat(left: titleValue, right: "' aria-label='"),
    ariaValue := str.concat(left: withAria, right: input.title),
    openEnd := str.concat(left: ariaValue, right: "'>"),
    content := str.concat(left: openEnd, right: input.label),
    return (str.concat(left: content, right: "</button>")).

RenderAnchor(input: any) =>
    start := str.concat(left: "<a href='", right: input.href),
    withClass := str.concat(left: start, right: "' class='"),
    classValue := str.concat(left: withClass, right: input.class),
    openEnd := str.concat(left: classValue, right: "' target='_blank' rel='noreferrer'>"),
    withLabel := str.concat(left: openEnd, right: input.label),
    return (str.concat(left: withLabel, right: "</a>")).

RenderParagraph(text: string) =>
    return (RenderHtmlTag(name: "p", id: "", class: "", content: text)).

RenderNote(text: string) =>
    return (RenderHtmlTag(name: "p", id: "", class: "note", content: text)).

RenderMiniCard(input: any) =>
    return (RenderMiniCardContent(input: HtmlCardContentData(title: input.title, content: RenderParagraph(text: input.text)))).

RenderMiniCardContent(input: any) =>
    title := RenderHtmlTag(name: "h3", id: "", class: "", content: input.title),
    body := str.concat(left: title, right: input.content),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "mini-card", content: body))).

RenderCardAction(title: string, text: string, action: string) =>
    intro := RenderParagraph(text: text),
    button := RenderButton(input: HtmlButtonData(label: action, id: "", class: "tool-button open-modal")),
    content := str.concat(left: intro, right: button),
    return (RenderMiniCardContent(input: HtmlCardContentData(title: title, content: content))).

RenderGrid(content: string) =>
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "grid", content: content))).

RenderModuleGrid(content: string) =>
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "module-grid", content: content))).

RenderDetails(title: string, text: string) =>
    summary := RenderHtmlTag(name: "summary", id: "", class: "", content: title),
    body := RenderParagraph(text: text),
    content := str.concat(left: summary, right: body),
    return (RenderHtmlTag(name: "details", id: "", class: "doc-detail", content: content)).

RenderDetailsContent(title: string, content: string) =>
    summary := RenderHtmlTag(name: "summary", id: "", class: "", content: title),
    body := str.concat(left: summary, right: content),
    return (RenderHtmlTag(name: "details", id: "", class: "doc-detail", content: body)).

RenderDetailsGrid(content: string) =>
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "details-grid", content: content))).

RenderGuidance(doText: string, dontText: string, recommendText: string) =>
    doPanel := RenderDetails(title: "Do", text: doText),
    dontPanel := RenderDetails(title: "Don't", text: dontText),
    recommendPanel := RenderDetails(title: "Recommendation", text: recommendText),
    part1 := str.concat(left: doPanel, right: dontPanel),
    part2 := str.concat(left: part1, right: recommendPanel),
    return (RenderDetailsGrid(content: part2)).

RenderCodeBlock(text: string) =>
    code := RenderHtmlTag(name: "code", id: "", class: "language-felidae", content: text),
    pre := RenderHtmlTag(name: "pre", id: "", class: "", content: code),
    actions := RenderCodeActions(text: text),
    inner := str.concat(left: actions, right: pre),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "code-box", content: inner))).

RenderCodeActions(text: string) =>
    button := RenderButton(input: HtmlButtonData(label: "Copy", id: "", class: "copy-code")),
    isFelidae := IsFelidaeCode(text: text),
    isFelidae == "true",
    download := RenderButton(input: HtmlButtonData(label: "Download", id: "", class: "download-code")),
    actionsContent := str.concat(left: button, right: download),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "code-actions", content: actionsContent)))
else
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "code-actions", content: button))).

IsFelidaeCode(text: string) =>
    hasRule := str.contains(data: text, needle: "=>"),
    hasRule == "true",
    return ("true")
else
    hasFact := str.contains(data: text, needle: ")."),
    hasFact == "true",
    return ("true")
else
    hasImport := str.contains(data: text, needle: "import "),
    hasImport == "true",
    return ("true")
else
    return ("false").

RenderSearchInput(id: string, placeholder: string) =>
    openId := str.concat(left: "<input id='", right: id),
    openClass := str.concat(left: openId, right: "' class='search-input' type='search' placeholder='"),
    withPlaceholder := str.concat(left: openClass, right: placeholder),
    return (str.concat(left: withPlaceholder, right: "' autocomplete='off'>")).

RenderSearchPanel(indexJson: string) =>
    title := RenderHtmlTag(name: "h2", id: "", class: "", content: "Search Documentation"),
    intro := RenderParagraph(text: "Search the Felidae documentation fact index generated by docs_search.fx."),
    input := RenderSearchInput(id: "docs-search", placeholder: "Search facts, queries, probability, modules..."),
    clear := RenderButton(input: HtmlButtonData(label: "Clear", id: "clear-search", class: "tool-button")),
    boxContent := str.concat(left: input, right: clear),
    box := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "search-box", content: boxContent)),
    results := RenderDiv(input: HtmlDivData(name: "div", id: "search-results", class: "search-results", content: "")),
    empty := RenderDiv(input: HtmlDivData(name: "div", id: "search-empty", class: "search-empty", content: "Type to search documentation facts.")),
    data := RenderSearchData(indexJson: indexJson),
    part1 := str.concat(left: title, right: intro),
    part2 := str.concat(left: part1, right: box),
    part3 := str.concat(left: part2, right: results),
    part4 := str.concat(left: part3, right: empty),
    part5 := str.concat(left: part4, right: data),
    return (RenderHtmlTag(name: "section", id: "docs-search-panel", class: "search-panel", content: part5)).

RenderSearchData(indexJson: string) =>
    start := "<script id='docs-search-index' type='application/json'>",
    withData := str.concat(left: start, right: indexJson),
    return (str.concat(left: withData, right: "</script>")).

# Inline SVG paths follow the free MIT-licensed Heroicons outline style.
IconSvg(path: string) =>
    start := "<svg class='icon-svg' viewBox='0 0 24 24' aria-hidden='true'>",
    withPath := str.concat(left: start, right: path),
    return (str.concat(left: withPath, right: "</svg>")).

IconPath(name: string) =>
    name == "home",
    return ("<path d='m2.25 12 8.954-8.955a1.125 1.125 0 0 1 1.591 0L21.75 12'/><path d='M4.5 9.75v10.125c0 .621.504 1.125 1.125 1.125H9.75v-4.875c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125V21h4.125c.621 0 1.125-.504 1.125-1.125V9.75'/>")
else
    name == "book",
    return ("<path d='M12 6.042A8.967 8.967 0 0 0 6 3.75c-1.052 0-2.062.18-3 .512v14.25A8.987 8.987 0 0 1 6 18c2.305 0 4.408.867 6 2.292m0-14.25A8.967 8.967 0 0 1 18 3.75c1.052 0 2.062.18 3 .512v14.25A8.987 8.987 0 0 0 18 18a8.967 8.967 0 0 0-6 2.292m0-14.25v14.25'/>")
else
    name == "play",
    return ("<path d='M5.25 5.653c0-1.427 1.529-2.33 2.779-1.643l11.54 6.347c1.295.712 1.295 2.573 0 3.286L8.03 19.99c-1.25.687-2.779-.216-2.779-1.643V5.653z'/>")
else
    name == "code",
    return ("<path d='M17.25 6.75 21 12l-3.75 5.25M6.75 6.75 3 12l3.75 5.25M14.25 4.5l-4.5 15'/>")
else
    name == "database",
    return ("<path d='M20.25 6.375c0 1.864-3.694 3.375-8.25 3.375S3.75 8.239 3.75 6.375 7.444 3 12 3s8.25 1.511 8.25 3.375z'/><path d='M20.25 6.375v11.25c0 1.864-3.694 3.375-8.25 3.375s-8.25-1.511-8.25-3.375V6.375'/><path d='M20.25 12c0 1.864-3.694 3.375-8.25 3.375S3.75 13.864 3.75 12'/>")
else
    name == "search",
    return ("<path d='m21 21-5.197-5.197m0 0A7.5 7.5 0 1 0 5.197 5.197a7.5 7.5 0 0 0 10.606 10.606z'/>")
else
    name == "terminal",
    return ("<path d='m6.75 7.5 3 3-3 3m4.5 0h6M3.75 4.5h16.5A1.5 1.5 0 0 1 21.75 6v12a1.5 1.5 0 0 1-1.5 1.5H3.75A1.5 1.5 0 0 1 2.25 18V6a1.5 1.5 0 0 1 1.5-1.5z'/>")
else
    name == "cube",
    return ("<path d='m21 7.5-9-5.25L3 7.5m18 0-9 5.25m9-5.25v9l-9 5.25M3 7.5l9 5.25M3 7.5v9l9 5.25m0-9v9'/>")
else
    name == "chart",
    return ("<path d='M3 13.125C3 12.504 3.504 12 4.125 12h2.25c.621 0 1.125.504 1.125 1.125v6.75C7.5 20.496 6.996 21 6.375 21h-2.25A1.125 1.125 0 0 1 3 19.875v-6.75zM9.75 8.625c0-.621.504-1.125 1.125-1.125h2.25c.621 0 1.125.504 1.125 1.125v11.25c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 0 1-1.125-1.125V8.625zM16.5 4.125c0-.621.504-1.125 1.125-1.125h2.25C20.496 3 21 3.504 21 4.125v15.75c0 .621-.504 1.125-1.125 1.125h-2.25a1.125 1.125 0 0 1-1.125-1.125V4.125z'/>")
else
    name == "cpu",
    return ("<path d='M8.25 3v1.5m7.5-1.5v1.5m-7.5 15V21m7.5-1.5V21M3 8.25h1.5m-1.5 7.5h1.5m15-7.5H21m-1.5 7.5H21M7.5 7.5h9v9h-9z'/><path d='M9.75 9.75h4.5v4.5h-4.5z'/>")
else
    name == "bug",
    return ("<path d='M12 12.75c1.148 0 2.278.08 3.383.237.58.083 1.003.589.93 1.17-.42 3.312-1.83 5.843-4.313 5.843s-3.893-2.531-4.313-5.843c-.073-.581.35-1.087.93-1.17A24.9 24.9 0 0 1 12 12.75z'/><path d='M12 12.75V6.75m0 0c0-1.243 1.007-2.25 2.25-2.25M12 6.75c0-1.243-1.007-2.25-2.25-2.25M6.75 14.25H4.5m15 0h-2.25M7.5 18l-2.25 1.5m11.25-1.5 2.25 1.5'/>")
else
    name == "cloud",
    return ("<path d='M2.25 15a4.5 4.5 0 0 0 4.5 4.5H18a3.75 3.75 0 0 0 1.332-7.257 5.25 5.25 0 0 0-10.233-2.33A4.5 4.5 0 0 0 2.25 15z'/>")
else
    name == "server",
    return ("<path d='M6 4.5h12A1.5 1.5 0 0 1 19.5 6v3A1.5 1.5 0 0 1 18 10.5H6A1.5 1.5 0 0 1 4.5 9V6A1.5 1.5 0 0 1 6 4.5zM6 13.5h12A1.5 1.5 0 0 1 19.5 15v3A1.5 1.5 0 0 1 18 19.5H6A1.5 1.5 0 0 1 4.5 18v-3A1.5 1.5 0 0 1 6 13.5z'/><path d='M8.25 7.5h.008v.008H8.25V7.5zm0 9h.008v.008H8.25V16.5z'/>")
else
    name == "download",
    return ("<path d='M3 16.5v2.25A2.25 2.25 0 0 0 5.25 21h13.5A2.25 2.25 0 0 0 21 18.75V16.5M7.5 10.5 12 15m0 0 4.5-4.5M12 15V3'/>")
else
    name == "tag",
    return ("<path d='M9.568 3H5.25A2.25 2.25 0 0 0 3 5.25v4.318c0 .597.237 1.169.659 1.591l9.182 9.182a2.25 2.25 0 0 0 3.182 0l4.318-4.318a2.25 2.25 0 0 0 0-3.182L11.159 3.659A2.25 2.25 0 0 0 9.568 3z'/><path d='M6 6h.008v.008H6V6z'/>")
else
    name == "flag",
    return ("<path d='M3.75 3v18M4.5 4.5h10.5l-.75 3 3 1.5-.75 3H4.5'/>")
else
    name == "info",
    return ("<path d='M11.25 11.25h1.5v6h-1.5z'/><path d='M12 7.5h.008v.008H12V7.5z'/><path d='M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0z'/>")
else
    name == "rocket",
    return ("<path d='M15.59 14.37a6 6 0 0 1-5.84 7.38v-4.8m5.84-2.58a14.98 14.98 0 0 0 6.16-12.12A14.98 14.98 0 0 0 9.63 8.41m5.96 5.96L9.63 8.41m0 0a6 6 0 0 0-7.38 5.84h4.8m2.58-5.84L7.5 10.5M14.25 6.75h.008v.008h-.008V6.75z'/>")
else
    name == "sun",
    return ("<path d='M12 3v2.25M12 18.75V21M4.22 4.22l1.59 1.59M18.19 18.19l1.59 1.59M3 12h2.25M18.75 12H21M4.22 19.78l1.59-1.59M18.19 5.81l1.59-1.59'/><path d='M15.75 12a3.75 3.75 0 1 1-7.5 0 3.75 3.75 0 0 1 7.5 0z'/>")
else
    name == "moon",
    return ("<path d='M21.75 15.75A9.75 9.75 0 0 1 8.25 2.25 7.5 7.5 0 1 0 21.75 15.75z'/>")
else
    return ("<path d='M12 6v12M6 12h12'/>").

HeroIcon(name: string) =>
    path := IconPath(name: name),
    return (IconSvg(path: path)).

RenderNavItem(input: any) =>
    start := str.concat(left: "<a href='", right: input.href),
    withClass := str.concat(left: start, right: "' class='' title='"),
    withTitle := str.concat(left: withClass, right: input.label),
    openEnd := str.concat(left: withTitle, right: "'>"),
    svg := HeroIcon(name: input.icon),
    iconStart := str.concat(left: "<span class='nav-icon'>", right: svg),
    icon := str.concat(left: iconStart, right: "</span>"),
    textStart := str.concat(left: "<span class='nav-text'>", right: input.label),
    text := str.concat(left: textStart, right: "</span>"),
    content := str.concat(left: icon, right: text),
    withContent := str.concat(left: openEnd, right: content),
    return (str.concat(left: withContent, right: "</a>")).

RenderNavBar() =>
    overview := RenderNavItem(input: navItem(label: "Overview", href: "#overview", order: 1, icon: "home")),
    basics := RenderNavItem(input: navItem(label: "Basics", href: "#basics", order: 2, icon: "book")),
    start := RenderNavItem(input: navItem(label: "Start", href: "#start", order: 3, icon: "play")),
    syntax := RenderNavItem(input: navItem(label: "Syntax", href: "#syntax", order: 4, icon: "code")),
    facts := RenderNavItem(input: navItem(label: "Facts DB", href: "#facts", order: 5, icon: "database")),
    queries := RenderNavItem(input: navItem(label: "Queries", href: "#queries", order: 6, icon: "search")),
    methods := RenderNavItem(input: navItem(label: "Methods", href: "#methods", order: 7, icon: "terminal")),
    reference := RenderNavItem(input: navItem(label: "Reference", href: "#reference", order: 8, icon: "book")),
    stdlib := RenderNavItem(input: navItem(label: "Stdlib", href: "#stdlib", order: 9, icon: "cube")),
    libraries := RenderNavItem(input: navItem(label: "Libraries", href: "#libraries", order: 10, icon: "cube")),
    probability := RenderNavItem(input: navItem(label: "Probability", href: "#probability", order: 11, icon: "chart")),
    native := RenderNavItem(input: navItem(label: "Native", href: "#native", order: 12, icon: "cpu")),
    debugging := RenderNavItem(input: navItem(label: "Debugging", href: "#debugging", order: 13, icon: "bug")),
    hosting := RenderNavItem(input: navItem(label: "Hosting", href: "#hosting", order: 14, icon: "cloud")),
    server := RenderNavItem(input: navItem(label: "Server", href: "#server", order: 15, icon: "server")),
    downloads := RenderNavItem(input: navItem(label: "Download", href: "#downloads", order: 16, icon: "download")),
    version := RenderNavItem(input: navItem(label: "Version", href: "#version", order: 17, icon: "tag")),
    milestones := RenderNavItem(input: navItem(label: "Milestones", href: "#milestones", order: 18, icon: "flag")),
    about := RenderNavItem(input: navItem(label: "About", href: "#about", order: 19, icon: "info")),
    playground := RenderNavItem(input: navItem(label: "Playground", href: "#playground", order: 20, icon: "rocket")),
    nav1 := str.concat(left: overview, right: basics),
    nav2 := str.concat(left: nav1, right: start),
    nav3 := str.concat(left: nav2, right: syntax),
    nav4 := str.concat(left: nav3, right: facts),
    nav5 := str.concat(left: nav4, right: queries),
    nav6 := str.concat(left: nav5, right: methods),
    nav7 := str.concat(left: nav6, right: reference),
    nav8 := str.concat(left: nav7, right: stdlib),
    nav9 := str.concat(left: nav8, right: libraries),
    nav10 := str.concat(left: nav9, right: probability),
    nav11 := str.concat(left: nav10, right: native),
    nav12 := str.concat(left: nav11, right: debugging),
    nav13 := str.concat(left: nav12, right: hosting),
    nav14 := str.concat(left: nav13, right: server),
    nav15 := str.concat(left: nav14, right: downloads),
    nav16 := str.concat(left: nav15, right: version),
    nav17 := str.concat(left: nav16, right: milestones),
    nav18 := str.concat(left: nav17, right: about),
    return (str.concat(left: nav18, right: playground)).

RenderSection(input: any) =>
    titleEnd := RenderHtmlTag(name: "h2", id: "", class: "", content: input.title),
    route := str.concat(left: "Route: /docs#", right: input.id),
    meta := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "route-meta", content: route)),
    p1 := RenderParagraph(text: input.p),
    p2 := RenderParagraph(text: input.p2),
    content := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "section-extra", content: input.content)),
    code1 := RenderCodeBlock(text: input.code),
    note := RenderNote(text: input.note),
    code2 := RenderCodeBlock(text: input.code2),
    routeActions := RenderRouteActions(),
    part1 := str.concat(left: titleEnd, right: meta),
    part2 := str.concat(left: part1, right: p1),
    part3 := str.concat(left: part2, right: p2),
    part4 := str.concat(left: part3, right: content),
    part5 := str.concat(left: part4, right: code1),
    part6 := str.concat(left: part5, right: note),
    part7 := str.concat(left: part6, right: code2),
    part8 := str.concat(left: part7, right: routeActions),
    return (RenderHtmlTag(name: "section", id: input.id, class: "route", content: part8)).

RenderPlayground(input: any) =>
    titleEnd := RenderHtmlTag(name: "h2", id: "", class: "", content: input.title),
    route := str.concat(left: "Route: /docs#", right: input.id),
    meta := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "route-meta", content: route)),
    intro := RenderParagraph(text: input.p),
    textarea := RenderTextarea(id: "playground-source", class: "playground-editor", content: input.example),
    copyProgram := RenderButton(input: HtmlButtonData(label: "Copy Program", id: "copy-playground", class: "tool-button")),
    copyCommand := RenderCommandButton(command: input.command),
    reset := RenderButton(input: HtmlButtonData(label: "Reset", id: "reset-playground", class: "tool-button")),
    tools1 := str.concat(left: copyProgram, right: copyCommand),
    tools2 := str.concat(left: tools1, right: reset),
    tools := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "playground-tools", content: tools2)),
    note := RenderNote(text: input.note),
    outputText := str.concat(left: "Run locally: ", right: input.command),
    output := RenderCodeBlock(text: outputText),
    routeActions := RenderRouteActions(),
    part1 := str.concat(left: titleEnd, right: meta),
    part2 := str.concat(left: part1, right: intro),
    part3 := str.concat(left: part2, right: textarea),
    part4 := str.concat(left: part3, right: tools),
    part5 := str.concat(left: part4, right: note),
    part6 := str.concat(left: part5, right: output),
    part7 := str.concat(left: part6, right: routeActions),
    return (RenderHtmlTag(name: "section", id: input.id, class: "route", content: part7)).

RenderTextarea(id: string, class: string, content: string) =>
    openId := str.concat(left: "<textarea id='", right: id),
    openClass := str.concat(left: openId, right: "' class='"),
    classValue := str.concat(left: openClass, right: class),
    openEnd := str.concat(left: classValue, right: "' spellcheck='false'>"),
    withContent := str.concat(left: openEnd, right: content),
    return (str.concat(left: withContent, right: "</textarea>")).

RenderCommandButton(command: string) =>
    start := str.concat(left: "<button id='copy-command' class='tool-button' data-command='", right: command),
    openEnd := str.concat(left: start, right: "'>"),
    label := str.concat(left: openEnd, right: "Copy Run Command"),
    return (str.concat(left: label, right: "</button>")).

RenderRouteActions() =>
    previous := RenderButton(input: HtmlButtonData(label: "Previous", id: "", class: "tool-button route-prev")),
    next := RenderButton(input: HtmlButtonData(label: "Next", id: "", class: "tool-button route-next")),
    details := RenderButton(input: HtmlButtonData(label: "Details", id: "", class: "tool-button open-modal")),
    part1 := str.concat(left: previous, right: next),
    part2 := str.concat(left: part1, right: details),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "", class: "route-actions", content: part2))).

RenderModalRoot() =>
    title := RenderHtmlTag(name: "h3", id: "modal-title", class: "", content: "Details"),
    close := RenderButton(input: HtmlButtonData(label: "Close", id: "", class: "modal-close")),
    headerContent := str.concat(left: title, right: close),
    header := RenderHtmlTag(name: "header", id: "", class: "", content: headerContent),
    body := RenderDiv(input: HtmlDivData(name: "div", id: "modal-body", class: "modal-body", content: "")),
    modalContent := str.concat(left: header, right: body),
    modal := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "modal", content: modalContent)),
    return (RenderDiv(input: HtmlDivData(name: "div", id: "modal-root", class: "modal-backdrop", content: modal))).

RenderHeader(nav: string) =>
    mark := RenderHtmlTag(name: "span", id: "", class: "felidae-mark", content: "Fx"),
    kicker := RenderParagraph(text: "Functional logic documentation"),
    title := RenderHtmlTag(name: "h1", id: "", class: "brand-title", content: "Felidae Documentation"),
    textBlock := str.concat(left: RenderDiv(input: HtmlDivData(name: "div", id: "", class: "brand-kicker", content: "Executable knowledge for code and data")), right: title),
    brandRowContent := str.concat(left: mark, right: RenderDiv(input: HtmlDivData(name: "div", id: "", class: "", content: textBlock))),
    brandRow := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "brand-row", content: brandRowContent)),
    heroTitle := RenderHtmlTag(name: "h2", id: "", class: "", content: "Facts that run, reason, and document themselves."),
    heroCopy := RenderParagraph(text: "Felidae turns documentation pages, runnable examples, fact indexes, native helpers, and search data into one browsable system served directly from .fx modules."),
    primaryAction := "<a href='#overview' class='hero-link primary'>Start reading</a>",
    searchAction := "<a href='#docs-search-panel' class='hero-link'>Search facts</a>",
    command := RenderDiv(input: HtmlDivData(name: "code", id: "", class: "hero-command", content: "./build/felidae.exe docs/server.fx")),
    actions1 := str.concat(left: primaryAction, right: searchAction),
    actionsContent := str.concat(left: actions1, right: command),
    actions := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-actions", content: actionsContent)),
    stat1 := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-stat", content: "<strong>.fx</strong><span>Language-native documentation</span>")),
    stat2 := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-stat", content: "<strong>SPA</strong><span>Generated static documentation shell</span>")),
    stat3 := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-stat", content: "<strong>Search</strong><span>Fact index powered by docs_search.fx</span>")),
    stats12 := str.concat(left: stat1, right: stat2),
    statsContent := str.concat(left: stats12, right: stat3),
    stats := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-stats", content: statsContent)),
    hero1 := str.concat(left: heroTitle, right: heroCopy),
    hero2 := str.concat(left: hero1, right: actions),
    heroContent := str.concat(left: hero2, right: stats),
    hero := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "hero-panel", content: heroContent)),
    rowHero := str.concat(left: brandRow, right: hero),
    brandContent := str.concat(left: kicker, right: rowHero),
    brand := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "header-brand", content: brandContent)),
    sunIcon := str.concat(left: "<span class='icon-sun'>", right: HeroIcon(name: "sun")),
    sun := str.concat(left: sunIcon, right: "</span>"),
    moonIcon := str.concat(left: "<span class='icon-moon'>", right: HeroIcon(name: "moon")),
    moon := str.concat(left: moonIcon, right: "</span>"),
    themeIcon := str.concat(left: sun, right: moon),
    themeToggle := RenderIconButton(input: HtmlIconButtonData(label: themeIcon, id: "theme-toggle", class: "theme-toggle", title: "Light")),
    searchButton := RenderIconButton(input: HtmlIconButtonData(label: HeroIcon(name: "search"), id: "focus-search", class: "theme-toggle", title: "Search")),
    toolsContent := str.concat(left: themeToggle, right: searchButton),
    tools := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "header-tools", content: toolsContent)),
    topContent := str.concat(left: brand, right: tools),
    top := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "header-top", content: topContent)),
    navTag := RenderHtmlTag(name: "nav", id: "", class: "", content: nav),
    part1 := str.concat(left: top, right: navTag),
    return (RenderHtmlTag(name: "header", id: "", class: "", content: part1)).

RenderFooter() =>
    link := RenderAnchor(input: HtmlAnchorData(label: "Follow on GitHub", href: "https://github.com/vishalkrishnaag/logicPrompts", class: "")),
    text := RenderParagraph(text: "Felidae docs generated from the live .fx documentation modules."),
    content := str.concat(left: text, right: link),
    grid := RenderDiv(input: HtmlDivData(name: "div", id: "", class: "footer-grid", content: content)),
    return (RenderHtmlTag(name: "footer", id: "", class: "", content: grid)).

RenderMain(content: string) =>
    return (RenderHtmlTag(name: "main", id: "", class: "", content: content)).

RenderDocumentStart(style: string) =>
    beforeStyle := "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Felidae Documentation</title><style>",
    withStyle := str.concat(left: beforeStyle, right: style),
    return (str.concat(left: withStyle, right: "</style></head><body>")).

RenderDocumentEnd() =>
    return ("</body></html>").

HtmlClientScript() =>
    router := HtmlRouterScript(),
    theme := HtmlThemeScript(),
    copy := HtmlCopyScript(),
    modal := HtmlModalScript(),
    search := HtmlSearchScript(),
    highlight := HtmlHighlightScript(),
    body1 := str.concat(left: router, right: theme),
    body2 := str.concat(left: body1, right: copy),
    body3 := str.concat(left: body2, right: search),
    body4 := str.concat(left: body3, right: modal),
    body5 := str.concat(left: body4, right: highlight),
    return (str.concat(left: "<script>(function(){", right: str.concat(left: body5, right: "document.addEventListener('DOMContentLoaded',function(){route();highlightAll();initSearch();initTheme()});route();applyTheme(localStorage.getItem('felidae-docs-theme')||'dark')})();</script>"))).

HtmlRouterScript() =>
    return ("function byId(id){return document.getElementById(id)}function routeIds(){return Array.from(document.querySelectorAll('nav a')).map(function(a){return a.getAttribute('href').slice(1)})}function currentId(){if(location.hash){return location.hash.slice(1)}var last=location.pathname.split('/').filter(Boolean).pop();return last&&byId(last)?last:'overview'}function showRoute(id,replace){var target=byId(id)||byId('overview');document.querySelectorAll('.route').forEach(function(s){s.classList.toggle('active',s===target)});document.querySelectorAll('nav a').forEach(function(a){var active=a.getAttribute('href')==='#'+target.id;a.classList.toggle('active',active);if(active){a.setAttribute('aria-current','page')}else{a.removeAttribute('aria-current')}});document.title='Felidae Documentation - '+target.querySelector('h2').innerText;if(replace&&location.hash!=='#'+target.id){history.replaceState(null,'','#'+target.id)}window.scrollTo(0,0)}function route(){showRoute(currentId(),true)}function move(delta){var ids=routeIds();var now=(document.querySelector('.route.active')||byId('overview')).id;var index=ids.indexOf(now);var next=ids[(index+delta+ids.length)%ids.length];location.hash=next;showRoute(next,false)}window.addEventListener('hashchange',route);").

HtmlThemeScript() =>
    return ("function applyTheme(mode){var light=mode==='light';document.body.classList.toggle('theme-light',light);var btn=byId('theme-toggle');if(btn){btn.title=light?'Dark':'Light';btn.setAttribute('aria-label',light?'Dark':'Light')}}function initTheme(){var saved=localStorage.getItem('felidae-docs-theme')||'dark';applyTheme(saved);var btn=byId('theme-toggle');if(btn){btn.addEventListener('click',function(){var next=document.body.classList.contains('theme-light')?'dark':'light';localStorage.setItem('felidae-docs-theme',next);applyTheme(next)})}}").

HtmlCopyScript() =>
    return ("function codeText(button){var box=button.closest('.code-box');var code=box?box.querySelector('code'):null;return code?(code.dataset.raw||code.innerText):''}function copyText(text,button){navigator.clipboard.writeText(text).then(function(){var old=button.textContent;button.textContent='Copied';button.classList.add('done');setTimeout(function(){button.textContent=old;button.classList.remove('done')},1100)})}function downloadText(text,name,button){var blob=new Blob([text],{type:'text/plain'});var a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;document.body.appendChild(a);a.click();var old=button.textContent;button.textContent='Saved';button.classList.add('done');setTimeout(function(){URL.revokeObjectURL(a.href);a.remove();button.textContent=old;button.classList.remove('done')},900)}document.addEventListener('click',function(e){var nav=e.target.closest('nav a');if(nav){e.preventDefault();var id=nav.getAttribute('href').slice(1);location.hash=id;showRoute(id,false)}var focusSearch=e.target.closest('#focus-search');if(focusSearch){document.body.classList.toggle('search-open');focusSearch.setAttribute('aria-expanded',document.body.classList.contains('search-open')?'true':'false');var input=byId('docs-search');if(input&&document.body.classList.contains('search-open')){input.scrollIntoView({behavior:'smooth',block:'center'});input.focus()}}var copy=e.target.closest('.copy-code');if(copy){copyText(codeText(copy),copy)}var dl=e.target.closest('.download-code');if(dl){downloadText(codeText(dl),'felidae-snippet.fx',dl)}var cp=e.target.closest('#copy-playground');if(cp){copyText(byId('playground-source').value,cp)}var cc=e.target.closest('#copy-command');if(cc){copyText(cc.getAttribute('data-command'),cc)}var rs=e.target.closest('#reset-playground');if(rs){var box=byId('playground-source');box.value=box.defaultValue}if(e.target.closest('.route-prev')){move(-1)}if(e.target.closest('.route-next')){move(1)}});").

HtmlModalScript() =>
    return ("document.addEventListener('click',function(e){var info=e.target.closest('.open-modal');if(info){var active=document.querySelector('.route.active');byId('modal-title').innerText=active.querySelector('h2').innerText;byId('modal-body').innerHTML=active.querySelector('p').outerHTML;byId('modal-root').classList.add('open')}if(e.target.closest('.modal-close')||e.target.id==='modal-root'){byId('modal-root').classList.remove('open')}});").

HtmlSearchScript() =>
    return ("function searchEsc(s){return String(s||'').replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}function readSearchIndex(){var node=byId('docs-search-index');if(!node){return []}try{return JSON.parse(node.textContent)||[]}catch(e){return []}}function renderSearchResults(rows,q){var out=byId('search-results');var empty=byId('search-empty');if(!out||!empty){return}out.innerHTML='';if(!q){empty.textContent='Type to search documentation facts.';return}var limited=rows.slice(0,8);limited.forEach(function(row){var a=document.createElement('a');a.className='search-result';a.href=row.route||('#'+row.id);a.innerHTML='<strong>'+searchEsc(row.title)+'</strong><span>'+searchEsc(row.summary)+'</span>';out.appendChild(a)});empty.textContent=rows.length?rows.length+' result(s) from DocSearchEntry facts.':'No documentation facts matched.'}function initSearch(){var input=byId('docs-search');if(!input){return}var rows=readSearchIndex();function run(){var q=input.value.trim().toLowerCase();var matches=q?rows.filter(function(row){return String(row.searchText||row.title||'').toLowerCase().indexOf(q)>=0||String(row.tags||'').toLowerCase().indexOf(q)>=0}):[];renderSearchResults(matches,q)}input.addEventListener('input',run);var clear=byId('clear-search');if(clear){clear.addEventListener('click',function(){input.value='';run();input.focus()})}run()}").

HtmlHighlightScript() =>
    return ("function esc(s){return s.replace(/[&<>]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}function hi(s){s=esc(s);s=s.replace(/(#.*)$/gm,'<span class=\"tok-comment\">$1</span>');s=s.replace(/(\"[^\"]*\")/g,'<span class=\"tok-str\">$1</span>');s=s.replace(/\\b(import|return|where|else|extend|main|lambda)\\b/g,'<span class=\"tok-key\">$1</span>');s=s.replace(/\\b([0-9]+(?:\\.[0-9]+)?)\\b/g,'<span class=\"tok-num\">$1</span>');s=s.replace(/\\b([A-Za-z_][A-Za-z0-9_]*)\\s*(?=\\()/g,'<span class=\"tok-fn\">$1</span>');return s}function highlightAll(){document.querySelectorAll('code.language-felidae').forEach(function(c){if(!c.dataset.raw){c.dataset.raw=c.innerText;c.innerHTML=hi(c.dataset.raw)}})}").

RenderHtmlShell(body: string, searchData: string) =>
    style := HtmlStyleSheet(),
    nav := RenderNavBar(),
    docStart := RenderDocumentStart(style: style),
    header := RenderHeader(nav: nav),
    searchPanel := RenderSearchPanel(indexJson: searchData),
    mainContent := str.concat(left: searchPanel, right: body),
    main := RenderMain(content: mainContent),
    footer := RenderFooter(),
    script := HtmlClientScript(),
    modal := RenderModalRoot(),
    end := RenderDocumentEnd(),
    part1 := str.concat(left: docStart, right: header),
    part2 := str.concat(left: part1, right: main),
    part3 := str.concat(left: part2, right: footer),
    part4 := str.concat(left: part3, right: modal),
    part5 := str.concat(left: part4, right: script),
    return (str.concat(left: part5, right: end)).
