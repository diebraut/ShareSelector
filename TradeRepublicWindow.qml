import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtWebView

Window {
    id: tradeRepublicWindow

    title: "Trade Republic Depot"
    visible: false
    flags: Qt.Window
    minimumWidth: 320
    minimumHeight: 420
    color: "#ffffff"

    property url depotUrl: "https://app.traderepublic.com/"
    property string loginPhoneNumber: tradeRepublicLoginPhone || ""
    property string loginPin: tradeRepublicLoginPin || ""
    property bool browserReady: false
    property var pendingPortfolioReadCallback: null
    property int portfolioReadPollAttempts: 0
    property bool portfolioReadPollPending: false

    Timer {
        id: portfolioReadPollTimer
        interval: 250
        repeat: true
        onTriggered: tradeRepublicWindow.pollPortfolioReadResult()
    }

    onClosing: function(closeEvent) {
        browserReady = false

        if (pendingPortfolioReadCallback) {
            completePortfolioRead({
                success: false,
                message: "Die Portfolio-Prüfung wurde durch das Schließen beendet.",
                positions: []
            })
        }

        const view = browserView()
        if (view)
            view.stop()

        closeEvent.accepted = true
        Qt.callLater(function() {
            tradeRepublicLoader.active = false
        })
    }

    function browserView() {
        return tradeRepublicLoader.item
    }

    function installSameWindowNavigation() {
        const view = browserView()
        if (!view)
            return

        view.runJavaScript(`
            (function() {
                if (window.__shareSelectorSameWindowNavigation)
                    return;
                window.__shareSelectorSameWindowNavigation = true;

                window.open = function(url) {
                    if (url)
                        window.location.assign(url);
                    return window;
                };

                document.addEventListener('click', function(event) {
                    var element = event.target;
                    if (!element || !element.closest)
                        return;
                    var link = element.closest('a[target="_blank"]');
                    if (!link || !link.href)
                        return;
                    event.preventDefault();
                    event.stopPropagation();
                    window.location.assign(link.href);
                }, true);
            })();
        `)
    }

    function fillLoginPhoneNumber() {
        const view = browserView()
        if (!view)
            return

        const phoneNumberJson = JSON.stringify(
            loginPhoneNumber.replace(/\D/g, "")
        )
        const loginPinJson = JSON.stringify(loginPin.replace(/\D/g, ""))
        view.runJavaScript(`
            (function() {
                if (window.__shareSelectorPhoneFillTimer)
                    window.clearInterval(window.__shareSelectorPhoneFillTimer);

                var phoneNumber = ${phoneNumberJson};
                var loginPin = ${loginPinJson};
                var attempts = 0;

                function isVisible(element) {
                    if (!element)
                        return false;
                    var style = window.getComputedStyle(element);
                    return style.display !== 'none'
                        && style.visibility !== 'hidden'
                        && element.getClientRects().length > 0;
                }

                function recoverExpiredLoginAttempt() {
                    var pageText = (document.body && document.body.innerText || '')
                        .toLowerCase();
                    var expired = pageText.indexOf(
                        'anmeldeversuch abgelaufen'
                    ) >= 0 || pageText.indexOf('login attempt expired') >= 0;
                    if (!expired)
                        return false;
                    window.sessionStorage.removeItem(
                        'shareSelectorSubmitted_phone'
                    );
                    window.sessionStorage.removeItem(
                        'shareSelectorSubmitted_pin'
                    );
                    return true;
                }

                function findPhoneInput() {
                    var explicit = document.querySelector(
                        'input[type="tel"], '
                        + 'input[autocomplete="tel"], '
                        + 'input[name*="phone" i], '
                        + 'input[placeholder*="phone" i], '
                        + 'input[placeholder*="telefon" i]'
                    );
                    if (isVisible(explicit))
                        return explicit;

                    var pageText = (document.body && document.body.innerText || '')
                        .toLowerCase();
                    if (pageText.indexOf('phone number') < 0
                            && pageText.indexOf('telefonnummer') < 0)
                        return null;

                    var inputs = Array.prototype.slice.call(
                        document.querySelectorAll('input')
                    );
                    return inputs.find(function(input) {
                        return isVisible(input)
                            && (input.type === 'text' || input.type === 'tel');
                    }) || null;
                }

                function continueLogin(input, credentialKind) {
                    var submittedKey = 'shareSelectorSubmitted_' + credentialKind;
                    if (window.sessionStorage.getItem(submittedKey) === '1')
                        return;

                    if (window.__shareSelectorCredentialSubmitTimer)
                        window.clearInterval(
                            window.__shareSelectorCredentialSubmitTimer
                        );

                    var submitAttempts = 0;
                    var minimumAttempts = credentialKind === 'pin' ? 4 : 1;
                    window.__shareSelectorCredentialSubmitTimer = window.setInterval(
                        function() {
                            submitAttempts += 1;
                            var scope = input.form || document;
                            var buttons = Array.prototype.slice.call(
                                scope.querySelectorAll(
                                    'button, input[type="submit"]'
                                )
                            );
                            var button = buttons.find(function(candidate) {
                                var label = (
                                    candidate.innerText
                                    || candidate.value
                                    || candidate.getAttribute('aria-label')
                                    || ''
                                ).trim().toLowerCase();
                                var isContinueButton = candidate.type === 'submit'
                                    || label === 'weiter'
                                    || label === 'next'
                                    || label === 'continue'
                                    || label === 'fortfahren';
                                return isContinueButton
                                    && isVisible(candidate)
                                    && !candidate.disabled
                                    && candidate.getAttribute('aria-disabled')
                                        !== 'true';
                            });

                            if (button && submitAttempts >= minimumAttempts) {
                                window.clearInterval(
                                    window.__shareSelectorCredentialSubmitTimer
                                );
                                window.__shareSelectorCredentialSubmitTimer = 0;
                                window.sessionStorage.setItem(submittedKey, '1');
                                button.click();
                            } else if (submitAttempts >= 20) {
                                window.clearInterval(
                                    window.__shareSelectorCredentialSubmitTimer
                                );
                                window.__shareSelectorCredentialSubmitTimer = 0;
                            }
                        },
                        150
                    );
                }

                function setInputValue(input, value) {
                    var descriptor = Object.getOwnPropertyDescriptor(
                        window.HTMLInputElement.prototype,
                        'value'
                    );
                    if (descriptor && descriptor.set)
                        descriptor.set.call(input, value);
                    else
                        input.value = value;

                    input.dispatchEvent(new Event('input', { bubbles: true }));
                    input.dispatchEvent(new Event('change', { bubbles: true }));
                }

                function findPinInputs() {
                    var pageText = (document.body && document.body.innerText || '')
                        .toLowerCase();
                    if (pageText.indexOf('pin') < 0)
                        return [];

                    var explicit = Array.prototype.slice.call(
                        document.querySelectorAll(
                            'input[type="password"], '
                            + 'input[autocomplete="current-password"], '
                            + 'input[name*="pin" i], '
                            + 'input[aria-label*="pin" i]'
                        )
                    ).filter(isVisible);
                    if (explicit.length > 0)
                        return explicit;

                    return Array.prototype.slice.call(
                        document.querySelectorAll(
                            'input[inputmode="numeric"], input[type="number"]'
                        )
                    ).filter(isVisible);
                }

                function setPinNumber() {
                    var pageText = (document.body && document.body.innerText || '')
                        .toLowerCase();
                    if (pageText.indexOf('anmeldeversuch abgelaufen') >= 0
                            || pageText.indexOf('login attempt expired') >= 0)
                        return false;
                    if (pageText.indexOf('erneut versuchen in') >= 0
                            || pageText.indexOf('try again in') >= 0)
                        return true;

                    if (window.sessionStorage.getItem(
                            'shareSelectorSubmitted_pin'
                        ) === '1')
                        return true;

                    var pinInputs = findPinInputs();
                    if (pinInputs.length === 0)
                        return false;

                    if (pinInputs.length === 1) {
                        setInputValue(pinInputs[0], loginPin);
                        pinInputs[0].focus();
                        continueLogin(pinInputs[0], 'pin');
                        return true;
                    }

                    if (pinInputs.length >= loginPin.length) {
                        for (var index = 0; index < loginPin.length; ++index)
                            setInputValue(pinInputs[index], loginPin[index]);
                        pinInputs[loginPin.length - 1].focus();
                        continueLogin(pinInputs[loginPin.length - 1], 'pin');
                        return true;
                    }

                    return false;
                }

                function startLeverageNavigation() {
                    if (window.__shareSelectorLeverageTimer)
                        window.clearInterval(
                            window.__shareSelectorLeverageTimer
                        );

                    var navigationAttempts = 0;
                    window.__shareSelectorLeverageTimer = window.setInterval(
                        function() {
                            navigationAttempts += 1;

                            if (/leverage/i.test(window.location.pathname)) {
                                window.clearInterval(
                                    window.__shareSelectorLeverageTimer
                                );
                                window.__shareSelectorLeverageTimer = 0;
                                return;
                            }

                            var candidates = Array.prototype.slice.call(
                                document.querySelectorAll(
                                    'a, button, [role="button"], [role="tab"]'
                                )
                            );
                            var leverageEntry = candidates.find(function(element) {
                                var label = (
                                    element.innerText
                                    || element.textContent
                                    || element.getAttribute('aria-label')
                                    || ''
                                ).trim().toLowerCase();
                                return label === 'leverage' && isVisible(element);
                            });

                            if (leverageEntry) {
                                window.clearInterval(
                                    window.__shareSelectorLeverageTimer
                                );
                                window.__shareSelectorLeverageTimer = 0;
                                window.sessionStorage.removeItem(
                                    'shareSelectorSubmitted_phone'
                                );
                                window.sessionStorage.removeItem(
                                    'shareSelectorSubmitted_pin'
                                );
                                leverageEntry.click();
                            } else if (navigationAttempts >= 1200) {
                                window.clearInterval(
                                    window.__shareSelectorLeverageTimer
                                );
                                window.__shareSelectorLeverageTimer = 0;
                            }
                        },
                        500
                    );
                }

                function setPhoneNumber() {
                    attempts += 1;
                    if (window.sessionStorage.getItem(
                            'shareSelectorSubmitted_phone'
                        ) === '1') {
                        window.clearInterval(
                            window.__shareSelectorPhoneFillTimer
                        );
                        window.__shareSelectorPhoneFillTimer = 0;
                        return;
                    }
                    var input = findPhoneInput();
                    if (!input) {
                        if (attempts >= 60) {
                            window.clearInterval(
                                window.__shareSelectorPhoneFillTimer
                            );
                            window.__shareSelectorPhoneFillTimer = 0;
                        }
                        return;
                    }

                    setInputValue(input, phoneNumber);
                    input.focus();
                    continueLogin(input, 'phone');

                    window.clearInterval(window.__shareSelectorPhoneFillTimer);
                    window.__shareSelectorPhoneFillTimer = 0;
                }

                var recoveringExpiredLogin = recoverExpiredLoginAttempt();
                setPhoneNumber();
                if (!window.__shareSelectorPhoneFillTimer) {
                    window.__shareSelectorPhoneFillTimer = window.setInterval(
                        setPhoneNumber,
                        250
                    );
                }

                if (window.__shareSelectorPinFillTimer)
                    window.clearInterval(window.__shareSelectorPinFillTimer);
                var pinAttempts = 0;
                window.__shareSelectorPinFillTimer = window.setInterval(
                    function() {
                        pinAttempts += 1;
                        if (setPinNumber() || pinAttempts >= 80) {
                            window.clearInterval(
                                window.__shareSelectorPinFillTimer
                            );
                            window.__shareSelectorPinFillTimer = 0;
                        }
                    },
                    250
                );
                if (!recoveringExpiredLogin)
                    setPinNumber();
                startLeverageNavigation();
            })();
        `)
    }

    function completePortfolioRead(result) {
        portfolioReadPollTimer.stop()
        portfolioReadPollPending = false
        const callback = pendingPortfolioReadCallback
        pendingPortfolioReadCallback = null
        if (callback)
            callback(result)
    }

    function pollPortfolioReadResult() {
        if (!pendingPortfolioReadCallback || portfolioReadPollPending)
            return

        const view = browserView()
        if (!view) {
            completePortfolioRead({
                success: false,
                message: "Das Trade-Republic-Fenster wurde geschlossen.",
                positions: []
            })
            return
        }

        portfolioReadPollAttempts += 1
        if (portfolioReadPollAttempts > 180) {
            completePortfolioRead({
                success: false,
                message: "Zeitüberschreitung beim Einlesen des Trade-Republic-Portfolios.",
                positions: []
            })
            return
        }

        portfolioReadPollPending = true
        view.runJavaScript(
            "window.__shareSelectorPortfolioReadResult || ''",
            function(rawResult) {
                tradeRepublicWindow.portfolioReadPollPending = false
                if (!tradeRepublicWindow.pendingPortfolioReadCallback
                        || !rawResult)
                    return
                try {
                    const result = typeof rawResult === "string"
                        ? JSON.parse(rawResult)
                        : rawResult
                    tradeRepublicWindow.completePortfolioRead(result)
                } catch (error) {
                    tradeRepublicWindow.completePortfolioRead({
                        success: false,
                        message: "Die gelesenen Portfolio-Daten sind ungültig.",
                        positions: []
                    })
                }
            }
        )
    }

    function readPortfolioPositions(callback) {
        const view = browserView()
        if (!view) {
            callback({
                success: false,
                message: "Das Trade-Republic-Fenster ist nicht geöffnet.",
                positions: []
            })
            return
        }

        if (pendingPortfolioReadCallback) {
            completePortfolioRead({
                success: false,
                message: "Die vorherige Portfolio-Prüfung wurde ersetzt.",
                positions: []
            })
        }
        pendingPortfolioReadCallback = callback
        portfolioReadPollAttempts = 0
        portfolioReadPollPending = false

        view.runJavaScript(`
            (function() {
                if (window.__shareSelectorPortfolioReadTimer)
                    window.clearInterval(
                        window.__shareSelectorPortfolioReadTimer
                    );
                window.__shareSelectorPortfolioReadTimer = 0;
                window.__shareSelectorPortfolioReadResult = '';

                function normalizedText(element) {
                    return (element && element.innerText || '')
                        .replace(/\\s+/g, ' ')
                        .trim();
                }

                var headings = Array.prototype.slice.call(
                    document.querySelectorAll(
                        'h1, h2, h3, h4, h5, h6, [role="heading"]'
                    )
                );
                var heading = headings.find(function(element) {
                    var text = normalizedText(element).toLowerCase();
                    return text === 'dein portfolio'
                        || text === 'your portfolio';
                });

                if (!heading) {
                    heading = Array.prototype.slice.call(
                        document.querySelectorAll('div, span, p')
                    ).find(function(element) {
                        if (element.children.length > 2)
                            return false;
                        var text = normalizedText(element).toLowerCase();
                        return text === 'dein portfolio'
                            || text === 'your portfolio';
                    });
                }

                if (!heading) {
                    window.__shareSelectorPortfolioReadResult = JSON.stringify({
                        success: false,
                        message: 'Der Bereich "Dein Portfolio" wurde nicht gefunden.',
                        positions: []
                    });
                    return 'finished';
                }

                function findExactElement(root, acceptedTexts) {
                    return Array.prototype.slice.call(
                        root.querySelectorAll(
                            'th, td, div, span, p, [role="columnheader"]'
                        )
                    ).find(function(element) {
                        var text = normalizedText(element).toLowerCase();
                        if (acceptedTexts.indexOf(text) < 0)
                            return false;
                        return !Array.prototype.slice.call(element.children)
                            .some(function(child) {
                                return normalizedText(child).toLowerCase() === text;
                            });
                    }) || null;
                }

                var section = null;
                var current = heading.parentElement;
                for (var level = 0; current && level < 10; ++level) {
                    var instrumentHeader = findExactElement(
                        current,
                        ['instrument']
                    );
                    var priceHeader = findExactElement(
                        current,
                        ['letzter kurs', 'last price']
                    );
                    if (instrumentHeader && priceHeader) {
                        section = current;
                        break;
                    }
                    current = current.parentElement;
                }

                if (!section)
                    section = heading.parentElement;

                var seen = {};
                var positions = [];

                function parseDisplayedNumber(value) {
                    var text = String(value || '')
                        .replace(/\u00a0/g, ' ')
                        .replace(/[^0-9,.-]/g, '')
                        .trim();
                    if (!text)
                        return null;

                    var comma = text.lastIndexOf(',');
                    var dot = text.lastIndexOf('.');
                    if (comma >= 0 && dot >= 0) {
                        if (comma > dot)
                            text = text.replace(/\\./g, '').replace(',', '.');
                        else
                            text = text.replace(/,/g, '');
                    } else if (comma >= 0) {
                        text = text.replace(/\\./g, '').replace(',', '.');
                    } else if (/^-?\\d{1,3}(\\.\\d{3})+$/.test(text)) {
                        text = text.replace(/\\./g, '');
                    }

                    var number = Number(text);
                    return Number.isFinite(number) ? number : null;
                }

                function identifierForElement(element) {
                    var holder = element.closest(
                        '[data-isin], a[href*="/instrument/"]'
                    );
                    if (!holder)
                        holder = element.querySelector(
                            '[data-isin], a[href*="/instrument/"]'
                        );
                    if (!holder)
                        return '';

                    var identifier = (
                        holder.getAttribute('data-isin') || ''
                    ).toUpperCase();
                    if (!identifier && holder.href) {
                        var match = holder.href.match(
                            /\\/instrument\\/([^/?#]+)/i
                        );
                        identifier = match
                            ? decodeURIComponent(match[1]).toUpperCase()
                            : '';
                    }
                    return /^[A-Z]{2}[A-Z0-9]{9}[0-9]$/.test(identifier)
                        ? identifier
                        : '';
                }

                function addPosition(name, element, details) {
                    name = (name || '').replace(/\\s+/g, ' ').trim();
                    var lowerName = name.toLowerCase();
                    if (!name || !/[a-zäöüß]/i.test(name))
                        return;
                    if (['instrument', 'performance', 'bestand', 'quantity',
                            'letzter kurs', 'last price', 'veränd. %',
                            'change %', 'dein portfolio', 'your portfolio']
                            .indexOf(lowerName) >= 0)
                        return;

                    var isin = identifierForElement(element);
                    var existing = (isin && seen[isin]) || seen[lowerName];
                    if (existing) {
                        if (details && details.quantity !== null
                                && details.quantity !== undefined)
                            existing.quantity = details.quantity;
                        if (details && details.currentPrice !== null
                                && details.currentPrice !== undefined)
                            existing.currentPrice = details.currentPrice;
                        return;
                    }

                    var position = {
                        isin: isin,
                        name: name,
                        quantity: details ? details.quantity : null,
                        currentPrice: details ? details.currentPrice : null
                    };
                    seen[lowerName] = position;
                    if (isin)
                        seen[isin] = position;
                    positions.push(position);
                }

                function valueInColumn(columnHeader, nextHeader, rowRect) {
                    if (!columnHeader)
                        return null;

                    var columnRect = columnHeader.getBoundingClientRect();
                    var nextRect = nextHeader
                        ? nextHeader.getBoundingClientRect()
                        : null;
                    var left = columnRect.left - 24;
                    var right = nextRect
                        ? nextRect.left - 4
                        : columnRect.right + 110;
                    var columnCenter = (columnRect.left + columnRect.right) / 2;
                    var rowCenter = (rowRect.top + rowRect.bottom) / 2;
                    var bestText = '';
                    var bestScore = Number.POSITIVE_INFINITY;

                    Array.prototype.slice.call(
                        section.querySelectorAll('td, div, span, p')
                    ).forEach(function(candidate) {
                        var text = normalizedText(candidate);
                        if (!text || !/[0-9]/.test(text) || text.length > 40)
                            return;
                        if (Array.prototype.slice.call(candidate.children)
                                .some(function(child) {
                                    return normalizedText(child) === text;
                                }))
                            return;

                        var rect = candidate.getBoundingClientRect();
                        var centerX = (rect.left + rect.right) / 2;
                        var centerY = (rect.top + rect.bottom) / 2;
                        var rowDistance = Math.abs(centerY - rowCenter);
                        var columnDistance = Math.abs(centerX - columnCenter);
                        var score = rowDistance * 4 + columnDistance;
                        if (centerX < left || centerX >= right
                                || rowDistance > Math.max(12, rowRect.height * 0.7)
                                || score >= bestScore)
                            return;
                        bestScore = score;
                        bestText = text;
                    });

                    return parseDisplayedNumber(bestText);
                }

                function scanPositions() {
                    Array.prototype.slice.call(
                        section.querySelectorAll('tr, [role="row"]')
                    ).forEach(function(row) {
                        var cells = row.querySelectorAll(
                            'th, td, [role="cell"], [role="gridcell"]'
                        );
                        if (cells.length >= 2) {
                            var quantityIndex = cells.length >= 5 ? 2 : 1;
                            var priceIndex = cells.length >= 5 ? 3 : 2;
                            var quantity = cells.length > quantityIndex
                                ? parseDisplayedNumber(
                                    normalizedText(cells[quantityIndex])
                                )
                                : null;
                            var currentPrice = cells.length > priceIndex
                                ? parseDisplayedNumber(
                                    normalizedText(cells[priceIndex])
                                )
                                : null;
                            addPosition(
                                normalizedText(cells[0]),
                                cells[0],
                                {
                                    quantity: quantity,
                                    currentPrice: currentPrice
                                }
                            );
                        }
                    });

                    Array.prototype.slice.call(
                        section.querySelectorAll('a[href*="/instrument/"]')
                    ).forEach(function(link) {
                        var lines = (link.innerText || link.textContent || '')
                            .split(/\\r?\\n/)
                            .map(function(line) {
                                return line.replace(/\\s+/g, ' ').trim();
                            })
                            .filter(function(line) {
                                return line && /[a-zäöüß]/i.test(line);
                            });
                        if (lines.length > 0)
                            addPosition(lines[0], link);
                    });

                    var instrumentColumn = findExactElement(
                        section,
                        ['instrument']
                    );
                    var performanceColumn = findExactElement(
                        section,
                        ['performance', 'performance --']
                    );
                    var quantityColumn = findExactElement(
                        section,
                        ['bestand', 'quantity']
                    );
                    var priceColumn = findExactElement(
                        section,
                        ['letzter kurs', 'last price']
                    );
                    var changeColumn = findExactElement(
                        section,
                        ['veränd. %', 'veranderung %', 'change %']
                    );
                    if (instrumentColumn) {
                        var instrumentRect = instrumentColumn.getBoundingClientRect();
                        var performanceRect = performanceColumn
                            ? performanceColumn.getBoundingClientRect()
                            : null;
                        var leftMinimum = instrumentRect.left - 24;
                        var leftMaximum = performanceRect
                            ? performanceRect.left - 8
                            : instrumentRect.left + 260;

                        Array.prototype.slice.call(
                            section.querySelectorAll('div, span, p, a, button')
                        ).forEach(function(element) {
                            var text = normalizedText(element);
                            if (!text || text.indexOf('\\n') >= 0
                                    || text.length > 100)
                                return;
                            if (Array.prototype.slice.call(element.children)
                                    .some(function(child) {
                                        return normalizedText(child) === text;
                                    }))
                                return;

                            var rect = element.getBoundingClientRect();
                            if (rect.top < instrumentRect.bottom - 1
                                    || rect.left < leftMinimum
                                    || rect.left >= leftMaximum
                                    || rect.height <= 0
                                    || rect.height > 48)
                                return;
                            addPosition(
                                text,
                                element,
                                {
                                    quantity: valueInColumn(
                                        quantityColumn,
                                        priceColumn,
                                        rect
                                    ),
                                    currentPrice: valueInColumn(
                                        priceColumn,
                                        changeColumn,
                                        rect
                                    )
                                }
                            );
                        });
                    }
                }

                function findScrollTarget() {
                    var candidates = [section].concat(
                        Array.prototype.slice.call(
                            section.querySelectorAll('*')
                        )
                    );
                    var best = null;
                    var bestOverflow = 0;

                    candidates.forEach(function(candidate) {
                        if (candidate.clientHeight < 80)
                            return;
                        var overflow = candidate.scrollHeight
                            - candidate.clientHeight;
                        if (overflow <= 8)
                            return;
                        var style = window.getComputedStyle(candidate);
                        if (style.overflowY !== 'auto'
                                && style.overflowY !== 'scroll')
                            return;
                        if (overflow > bestOverflow) {
                            best = candidate;
                            bestOverflow = overflow;
                        }
                    });

                    if (best)
                        return best;

                    var parent = section.parentElement;
                    for (var level = 0; parent && level < 8; ++level) {
                        var overflow = parent.scrollHeight - parent.clientHeight;
                        var style = window.getComputedStyle(parent);
                        if (parent.clientHeight >= 80 && overflow > 8
                                && (style.overflowY === 'auto'
                                    || style.overflowY === 'scroll'))
                            return parent;
                        parent = parent.parentElement;
                    }

                    return document.scrollingElement
                        || document.documentElement;
                }

                function finishPortfolioRead(scrollTarget, originalScrollTop) {
                    if (window.__shareSelectorPortfolioReadTimer)
                        window.clearInterval(
                            window.__shareSelectorPortfolioReadTimer
                        );
                    window.__shareSelectorPortfolioReadTimer = 0;

                    scanPositions();
                    if (scrollTarget) {
                        scrollTarget.scrollTop = originalScrollTop;
                        scrollTarget.dispatchEvent(
                            new Event('scroll', { bubbles: true })
                        );
                    }

                    var isinCount = positions.filter(function(position) {
                        return Boolean(position.isin);
                    }).length;
                    var quantityCount = positions.filter(function(position) {
                        return position.quantity !== null
                            && position.quantity !== undefined;
                    }).length;
                    var priceCount = positions.filter(function(position) {
                        return position.currentPrice !== null
                            && position.currentPrice !== undefined;
                    }).length;
                    window.__shareSelectorPortfolioReadResult = JSON.stringify({
                        success: positions.length > 0,
                        message: positions.length > 0
                            ? ''
                            : 'Die Portfolio-Positionen konnten nicht gelesen werden.',
                        positions: positions,
                        isinCount: isinCount,
                        quantityCount: quantityCount,
                        priceCount: priceCount
                    });
                }

                scanPositions();
                var scrollTarget = findScrollTarget();
                var originalScrollTop = scrollTarget
                    ? scrollTarget.scrollTop
                    : 0;
                var maximumScrollTop = scrollTarget
                    ? scrollTarget.scrollHeight - scrollTarget.clientHeight
                    : 0;

                if (!scrollTarget || maximumScrollTop <= 8) {
                    finishPortfolioRead(scrollTarget, originalScrollTop);
                    return 'finished';
                }

                scrollTarget.scrollTop = 0;
                scrollTarget.dispatchEvent(
                    new Event('scroll', { bubbles: true })
                );

                var stepCount = 0;
                var bottomPasses = 0;
                var lastCount = positions.length;
                window.__shareSelectorPortfolioReadTimer = window.setInterval(
                    function() {
                        stepCount += 1;
                        scanPositions();

                        var maximum = Math.max(
                            0,
                            scrollTarget.scrollHeight - scrollTarget.clientHeight
                        );
                        var currentTop = scrollTarget.scrollTop;
                        var atBottom = currentTop >= maximum - 3;
                        if (atBottom) {
                            if (positions.length === lastCount)
                                bottomPasses += 1;
                            else
                                bottomPasses = 0;
                        } else {
                            bottomPasses = 0;
                            var step = Math.max(
                                100,
                                Math.floor(scrollTarget.clientHeight * 0.65)
                            );
                            var nextTop = Math.min(maximum, currentTop + step);
                            scrollTarget.scrollTop = nextTop;
                            scrollTarget.dispatchEvent(
                                new Event('scroll', { bubbles: true })
                            );
                        }

                        lastCount = positions.length;
                        if (bottomPasses >= 2 || stepCount >= 160)
                            finishPortfolioRead(
                                scrollTarget,
                                originalScrollTop
                            );
                    },
                    180
                );

                return 'started';
            })();
        `, function() {
            if (!tradeRepublicWindow.pendingPortfolioReadCallback)
                return
            portfolioReadPollTimer.start()
            tradeRepublicWindow.pollPortfolioReadResult()
        })
    }

    function openBeside(desktopLeft, depotX, depotY, depotHeight) {
        const left = Number(desktopLeft)
        const right = Number(depotX)
        const top = Number(depotY)
        const targetHeight = Number(depotHeight)

        if (!tradeRepublicLoader.active) {
            browserReady = false
            tradeRepublicLoader.active = true
        }

        tradeRepublicWindow.showNormal()
        tradeRepublicWindow.show()
        tradeRepublicWindow.x = isNaN(left) ? 0 : left
        tradeRepublicWindow.y = isNaN(top) ? 0 : top
        tradeRepublicWindow.width = Math.max(
            tradeRepublicWindow.minimumWidth,
            (isNaN(right) ? tradeRepublicWindow.minimumWidth : right)
                - tradeRepublicWindow.x
        )
        tradeRepublicWindow.height = Math.max(
            tradeRepublicWindow.minimumHeight,
            isNaN(targetHeight) ? tradeRepublicWindow.minimumHeight : targetHeight
        )
        tradeRepublicWindow.raise()
        tradeRepublicWindow.requestActivate()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 4

                ToolButton {
                    text: "\u2039"
                    enabled: tradeRepublicWindow.browserReady
                    onClicked: {
                        const view = tradeRepublicWindow.browserView()
                        if (view)
                            view.goBack()
                    }
                }

                ToolButton {
                    text: "\u203a"
                    enabled: tradeRepublicWindow.browserReady
                    onClicked: {
                        const view = tradeRepublicWindow.browserView()
                        if (view)
                            view.goForward()
                    }
                }

                ToolButton {
                    text: "\u21bb"
                    enabled: tradeRepublicLoader.item !== null
                    onClicked: {
                        const view = tradeRepublicWindow.browserView()
                        if (view)
                            view.reload()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: tradeRepublicLoader.item ? tradeRepublicLoader.item.url : ""
                    elide: Text.ElideMiddle
                    color: "#334155"
                }
            }
        }

        Loader {
            id: tradeRepublicLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: false

            sourceComponent: Component {
                WebView {
                    url: tradeRepublicWindow.depotUrl

                    onLoadingChanged: function(loadRequest) {
                        if (loadRequest.status === WebView.LoadStartedStatus) {
                            tradeRepublicWindow.browserReady = false
                        } else if (loadRequest.status === WebView.LoadSucceededStatus) {
                            tradeRepublicWindow.browserReady = true
                            tradeRepublicWindow.installSameWindowNavigation()
                            tradeRepublicWindow.fillLoginPhoneNumber()
                        } else if (loadRequest.status === WebView.LoadFailedStatus) {
                            tradeRepublicWindow.browserReady = false
                        }
                    }
                }
            }
        }
    }
}
