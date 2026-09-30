const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

// Exercise the actual QML JavaScript without requiring a browser or live depot.
const qml = fs.readFileSync(path.join(__dirname, '..', 'CheckDepotsWindow.qml'), 'utf8');
const functions = qml.slice(qml.indexOf('    function normalizedName('),
    qml.indexOf('    function checkTradeRepublicPositions('));
function context() {
    const result = vm.createContext({});
    vm.runInContext(functions, result);
    return result;
}
const stock = (name, isin = '') => ({ name, isin });

test('generic abbreviations cannot match Raiffeisen to SITC in either direction', () => {
    const ctx = context();
    for (const raiffeisen of ['RAIFFEISEN BANK INTERNAT.', 'RAIFFEISEN BANK INTERNATIONA']) {
        for (const sitc of ['SITC International Holdings', 'SITC INTERNATIONAL HLDG']) {
            assert.equal(ctx.positionMatchStrength(stock(raiffeisen), stock(sitc)), 0);
            assert.equal(ctx.positionMatchStrength(stock(sitc), stock(raiffeisen)), 0);
        }
    }
    assert.equal(ctx.positionMatchStrength(stock('Alpha Bank International'),
        stock('Beta Bank International')), 0);
});

test('legitimate abbreviated names, ISINs and share classes still work', () => {
    const ctx = context();
    assert.ok(ctx.positionMatchStrength(stock('RAIFFEISEN BANK INTERNAT.'),
        stock('Raiffeisen Bank International')));
    assert.ok(ctx.positionMatchStrength(stock('SITC International Holdings'), stock('SITC')));
    assert.ok(ctx.positionMatchStrength(stock('SITC INTERNATIONAL HLDG'),
        stock('SITC International Holdings')));
    assert.equal(ctx.positionMatchStrength(stock('Same', 'AT0000606306'),
        stock('Same', 'KYG8187G1055')), 0);
    assert.equal(ctx.positionMatchStrength(stock('Different', 'KYG8187G1055'),
        stock('Names', 'KYG8187G1055')), 3);
    assert.equal(ctx.positionMatchStrength(stock('Example (A)'), stock('Example (B)')), 0);
});

test('Raiffeisen does not consume the earlier SITC row or its price and quantity', () => {
    const ctx = context();
    ctx.observedStocks = [stock('RAIFFEISEN BANK INTERNAT.'), stock('SITC International Holdings')];
    ctx.applyPortfolioCheck({ success: true, positions: [
        { name: 'SITC International Holdings', currentPrice: 3, quantity: 100 },
        { name: 'Raiffeisen Bank International', currentPrice: 30, quantity: 10 }
    ] });
    assert.ok(ctx.observedStocks.every(row => row.checked));
    assert.equal(ctx.observedStocks[0].trCurrentPrice, 30);
    assert.equal(ctx.observedStocks[0].trQuantity, 10);
    assert.equal(ctx.observedStocks[1].trCurrentPrice, 3);
    assert.match(ctx.checkMessage, /Alle Positionen stimmen überein/);
});

test('exact names and ISINs are reserved before fuzzy matches', () => {
    for (const exactStock of [stock('Example Industries'), stock('Other name', 'AT0000606306')]) {
        const ctx = context();
        ctx.observedStocks = [stock('Example'), exactStock];
        ctx.applyPortfolioCheck({ success: true, positions: [
            { name: 'Example Industries', isin: 'AT0000606306', currentPrice: 50, quantity: 2 }
        ] });
        assert.equal(ctx.observedStocks[0].checked, false);
        assert.equal(ctx.observedStocks[0].trCurrentPrice, null);
        assert.equal(ctx.observedStocks[1].checked, true);
        assert.equal(ctx.observedStocks[1].trCurrentPrice, 50);
    }
});

test('Finnish Oyj suffix does not prevent YIT from matching', () => {
    const ctx = context();
    ctx.observedStocks = [stock('YIT OYJ')];
    ctx.applyPortfolioCheck({ success: true, positions: [
        { name: 'YIT', currentPrice: 2.50, quantity: 42 }
    ] });
    assert.equal(ctx.observedStocks[0].checked, true);
    assert.equal(ctx.observedStocks[0].trCurrentPrice, 2.50);
    assert.equal(ctx.observedStocks[0].trQuantity, 42);
    assert.match(ctx.checkMessage, /Alle Positionen stimmen überein/);
});
