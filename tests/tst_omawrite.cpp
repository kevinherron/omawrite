#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <QWheelEvent>

#include "backend.h"
#include "markdownhighlighter.h"

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void init() {
        QSettings().clear();
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void keepsNotoFromCrowdingTheFontList() {
        const QStringList fonts = Backend::selectableFontFamilies({
            QStringLiteral("Noto Sans Devanagari"), QStringLiteral("Liberation Serif"),
            QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Tamil UI"),
            QStringLiteral("adwaita Sans"), QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Liberation Serif"), QStringLiteral("Monospace"),
            QStringLiteral("Standard Symbols PS"), QStringLiteral("D050000L"),
            QStringLiteral("Noto Color Emoji"), QStringLiteral("Nimbus Sans [UKWN]"),
            QStringLiteral("Nimbus Sans [URW ]")});
        QCOMPARE(fonts, QStringList({QStringLiteral("IBM Plex Mono"),
                                     QStringLiteral("adwaita Sans"),
                                     QStringLiteral("Liberation Serif"),
                                     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Noto Sans"),
                                     QStringLiteral("Noto Sans Mono"),
                                     QStringLiteral("Noto Serif")}));
    }

    void remembersEditorFont() {
        const QString installed = Backend().availableFonts().value(1);
        QVERIFY(!installed.isEmpty());

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(window->findChild<QObject *>(QStringLiteral("previewButton")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void previewsMarkdownWithoutChangingTheSource() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("preview.md"));
        const QString markdown = QStringLiteral(
            "# Title\n\nA **bold** and *italic* paragraph.\n\n"
            "- first\n- second\n\n> quoted\n\n"
            "```cpp\nint value = 42;\n```\n\n"
            "| Name | Value |\n| --- | --- |\n| item | 42 |\n");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(markdown.toUtf8()), markdown.toUtf8().size());
        file.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        quickWindow->requestActivate();
        QTRY_VERIFY(quickWindow->isActive());

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        QObject *previewButton = window->findChild<QObject *>(QStringLiteral("previewButton"));
        QVERIFY(editor);
        QVERIFY(preview);
        QVERIFY(previewButton);
        QVERIFY(!window->property("previewActive").toBool());
        QVERIFY(!preview->property("visible").toBool());

        backend.open(QUrl::fromLocalFile(path));
        QCOMPARE(editor->property("text").toString(), markdown);
        QVERIFY(!backend.modified());
        QSignalSpy modifiedSpy(&backend, &Backend::modifiedChanged);

        QTest::keyClick(quickWindow, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(window->property("previewActive").toBool());
        QVERIFY(preview->property("visible").toBool());
        QVERIFY(preview->property("readOnly").toBool());
        QVERIFY(preview->property("activeFocus").toBool());
        QVERIFY(!editor->property("visible").toBool());
        QCOMPARE(preview->property("baseUrl").toUrl(), QUrl::fromLocalFile(path));
        QCOMPARE(modifiedSpy.count(), 0);
        QCOMPARE(editor->property("text").toString(), markdown);

        auto *quickDocument = preview->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(quickDocument);
        QTextDocument *rendered = quickDocument->textDocument();
        QCOMPARE(rendered->firstBlock().text(), QStringLiteral("Title"));
        QCOMPARE(rendered->firstBlock().blockFormat().headingLevel(), 1);
        QTextCursor bold = rendered->find(QStringLiteral("bold"));
        QVERIFY(!bold.isNull());
        QCOMPARE(bold.charFormat().fontWeight(), QFont::Bold);
        QVERIFY(rendered->find(QStringLiteral("italic")).charFormat().fontItalic());
        QVERIFY(rendered->find(QStringLiteral("first")).block().textList());
        QVERIFY(rendered->toPlainText().contains(QStringLiteral("int value = 42;")));
        bool hasTable = false;
        for (QTextFrame *frame : rendered->rootFrame()->childFrames())
            hasTable |= qobject_cast<QTextTable *>(frame) != nullptr;
        QVERIFY(hasTable);

        // Preview must not allow editing shortcuts to mutate the hidden editor.
        QTest::keyClick(quickWindow, Qt::Key_X);
        QTest::keyClick(quickWindow, Qt::Key_B, Qt::ControlModifier);
        QTest::keyClick(quickWindow, Qt::Key_I, Qt::ControlModifier);
        QTest::keyClick(quickWindow, Qt::Key_K, Qt::ControlModifier);
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->property("text").toString(), markdown);
        QCOMPARE(modifiedSpy.count(), 0);

        // Saving in preview still writes the original Markdown.
        backend.save();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), markdown.toUtf8());
        file.close();

        QTest::keyClick(quickWindow, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!window->property("previewActive").toBool());
        QVERIFY(editor->property("activeFocus").toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "insert",
                                         Q_ARG(int, markdown.size()),
                                         Q_ARG(QString, QStringLiteral("last edit"))));
        QVERIFY(editor->property("canUndo").toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 2), Q_ARG(int, 7)));
        const QString edited = editor->property("text").toString();
        modifiedSpy.clear();

        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QVERIFY(window->property("previewActive").toBool());
        QVERIFY(rendered->toPlainText().contains(QStringLiteral("last edit")));
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->property("text").toString(), edited);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("previewActive").toBool());
        QCOMPARE(editor->property("text").toString(), edited);
        QCOMPARE(editor->property("selectionStart").toInt(), 2);
        QCOMPARE(editor->property("selectionEnd").toInt(), 7);
        QCOMPARE(editor->property("cursorPosition").toInt(), 7);
        QCOMPARE(modifiedSpy.count(), 0);
        QTest::keyClick(quickWindow, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->property("text").toString(), markdown);

        // Find returns to the source and keeps focus in the search field.
        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QTest::keyClick(quickWindow, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(!window->property("previewActive").toBool());
        QVERIFY(window->property("searchOpen").toBool());
        QTRY_VERIFY(quickWindow->activeFocusItem() != editor);

        // Each mode keeps its own scroll position across repeated toggles.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "closeSearch"));
        QVERIFY(editor->setProperty("text", QStringLiteral("Paragraph **bold** text.\n\n")
                                              .repeated(100)));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(flick);
        QVERIFY(flick->setProperty("contentY", 600.0));
        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QTRY_COMPARE(flick->property("contentY").toReal(), 0.0);
        QVERIFY(flick->setProperty("contentY", 300.0));
        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QTRY_COMPARE(flick->property("contentY").toReal(), 600.0);
        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QTRY_COMPARE(flick->property("contentY").toReal(), 300.0);

        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(preview->property("font").value<QFont>().pixelSize(), 27);
        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(preview->property("font").value<QFont>().pixelSize(), 29);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 29);
        QTest::keyClick(quickWindow, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(preview->property("font").value<QFont>().pixelSize(), 27);
        QTest::keyClick(quickWindow, Qt::Key_Equal, Qt::ControlModifier);
        QTest::keyClick(quickWindow, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(preview->property("font").value<QFont>().pixelSize(), 27);

        // Switching modes cancels a touchpad gesture and its pending momentum.
        const QPointF position(quickWindow->width() / 2.0, quickWindow->height() / 2.0);
        sendTouchpadWheel(quickWindow, position, -8, -120, Qt::ScrollBegin);
        QTest::qWait(20);
        sendTouchpadWheel(quickWindow, position, -10, -120, Qt::ScrollUpdate);
        QVERIFY(flick->property("touchpadGestureActive").toBool());
        QVERIFY(QMetaObject::invokeMethod(previewButton, "clicked"));
        QTRY_COMPARE(flick->property("contentY").toReal(), 600.0);
        QVERIFY(!flick->property("touchpadGestureActive").toBool());
        QTest::qWait(120);
        QCOMPARE(flick->property("contentY").toReal(), 600.0);
        backend.discardRecovery();
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void centersWidePreviewContentAcrossZoomLevels() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(editor);
        QVERIFY(preview);
        QVERIFY(flick);
        const QString codeLine = QStringLiteral("    configuration-ref.md    # ")
            + QStringLiteral("LongConfigurationReference ").repeated(4);
        const QString markdown = QStringLiteral("# Heading\n\n```text\n") + codeLine
            + QStringLiteral("\n```\n\n") + QStringLiteral("A paragraph of text. ").repeated(30);
        QVERIFY(editor->setProperty("text", markdown));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));

        for (int windowWidth : {720, 1280, 1920}) {
            QVERIFY(window->setProperty("width", windowWidth));
            for (qreal textScale : {1.0, 16.0 / 12.0}) {
                backend.setTextScale(textScale);
                for (int fontSize : {10, 20, 30, 48}) {
                    backend.setEditorFontSize(fontSize);
                    const qreal viewportWidth = flick->property("width").toReal();
                    QTRY_VERIFY(preview->property("contentWidth").toReal() > 0);
                    const qreal renderedWidth = qMax(preview->property("width").toReal(),
                                                    preview->property("contentWidth").toReal());
                    if (renderedWidth <= viewportWidth) {
                        QTRY_VERIFY(qAbs(preview->property("x").toReal() * 2
                                         + renderedWidth - viewportWidth) <= 1.0);
                    } else {
                        QTRY_COMPARE(preview->property("x").toReal(), 0.0);
                        QVERIFY(flick->property("contentWidth").toReal() >= renderedWidth);
                    }
                    QCOMPARE(editor->property("text").toString(), markdown);
                }
            }
        }

        // A wide line remains intact and can be scrolled into view.
        auto *quickDocument = preview->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(quickDocument);
        QVERIFY(quickDocument->textDocument()->toPlainText().contains(codeLine));
        auto *quickWindow = qobject_cast<QQuickWindow *>(window.data());
        QVERIFY(quickWindow);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        QVERIFY(flick->setProperty("contentX", 100.0));
        const QPointF position(quickWindow->width() / 2.0, quickWindow->height() / 2.0);
        QWheelEvent horizontalWheel(position, quickWindow->mapToGlobal(position),
                                    QPoint(-20, 0), QPoint(), Qt::NoButton,
                                    Qt::NoModifier, Qt::ScrollUpdate, false);
        QCoreApplication::sendEvent(quickWindow, &horizontalWheel);
        QTRY_COMPARE(flick->property("contentX").toReal(), 140.0);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QTRY_COMPARE(flick->property("contentX").toReal(), 0.0);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QTRY_COMPARE(flick->property("contentX").toReal(), 140.0);

        // Removing the wide block restores the normal centered column.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QVERIFY(editor->setProperty("text", QStringLiteral("A paragraph of text. ").repeated(30)));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QTRY_COMPARE(flick->property("contentX").toReal(), 0.0);
        QTRY_VERIFY(qAbs(preview->property("x").toReal() * 2
                         + preview->property("width").toReal()
                         - flick->property("width").toReal()) <= 1.0);
        backend.discardRecovery();
    }

    void scalesTouchpadScrollingAndCarriesMomentum() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QObject *editor = root->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *flick = root->findChild<QObject *>(QStringLiteral("editorFlick"));
        QVERIFY(window);
        QVERIFY(editor);
        QVERIFY(flick);

        QStringList lines;
        for (int i = 0; i < 200; ++i)
            lines.append(QStringLiteral("A line long enough to make the editor scroll."));
        editor->setProperty("text", lines.join(QLatin1Char('\n')));
        QTRY_VERIFY(flick->property("contentHeight").toReal()
                    > flick->property("height").toReal() + 400);
        flick->setProperty("contentY", 200.0);

        const QPointF position(window->width() / 2.0, window->height() / 2.0);
        sendTouchpadWheel(window, position, -8, -120, Qt::ScrollBegin);
        QTest::qWait(20);
        sendTouchpadWheel(window, position, -10, -120, Qt::ScrollUpdate);
        QTest::qWait(20);
        sendTouchpadWheel(window, position, -10, -120, Qt::ScrollUpdate);

        const qreal gestureEndY = flick->property("contentY").toReal();
        QVERIFY2(qAbs(gestureEndY - 380.0) <= 2.0,
                 qPrintable(QStringLiteral("Expected angle-scaled contentY near 380, got %1")
                                .arg(gestureEndY)));

        sendTouchpadWheel(window, position, 0, 0, Qt::ScrollEnd);
        QTRY_VERIFY_WITH_TIMEOUT(flick->property("contentY").toReal()
                                 > gestureEndY + 5.0, 500);

        // Platform momentum replaces the fallback rather than adding to it.
        sendTouchpadWheel(window, position, -2, -24, Qt::ScrollBegin);
        sendTouchpadWheel(window, position, -6, -72, Qt::ScrollMomentum);
        const qreal platformMomentumY = flick->property("contentY").toReal();
        QVERIFY(flick->property("platformMomentumActive").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!flick->property("platformMomentumActive").toBool(),
                                 300);
        QVERIFY(qAbs(flick->property("contentY").toReal() - platformMomentumY) <= 1.0);

        // Pixel-only platforms keep direct movement and coast after an event gap.
        flick->setProperty("contentY", 200.0);
        sendTouchpadWheel(window, position, -8, 0, Qt::NoScrollPhase);
        QTest::qWait(20);
        sendTouchpadWheel(window, position, -10, 0, Qt::NoScrollPhase);
        const qreal pixelEndY = flick->property("contentY").toReal();
        QVERIFY(qAbs(pixelEndY - 236.0) <= 2.0);
        QTRY_VERIFY_WITH_TIMEOUT(flick->property("contentY").toReal() > pixelEndY + 5, 500);

        // A new gesture stops the coast immediately, even before movement.
        sendTouchpadWheel(window, position, 1, 0, Qt::ScrollBegin);
        const qreal stoppedY = flick->property("contentY").toReal();
        QTest::qWait(40);
        QVERIFY(qAbs(flick->property("contentY").toReal() - stoppedY) <= 1.0);
        sendTouchpadWheel(window, position, 0, 0, Qt::ScrollEnd);

        // Mouse-wheel input keeps the upstream animated 120px notch distance.
        flick->setProperty("contentY", 200.0);
        sendTouchpadWheel(window, position, 0, -120, Qt::NoScrollPhase);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(flick->property("contentY").toReal() - 320.0) <= 2.0, 500);

        // Momentum and direct movement never overshoot the content bounds.
        sendTouchpadWheel(window, position, 10000, 0, Qt::ScrollBegin);
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
        sendTouchpadWheel(window, position, 0, 0, Qt::ScrollEnd);
        QTest::qWait(100);
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
    }

    void dispatchesEditorFontSizeShortcuts() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        QObject *editor = root->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        const QString selectedFont = backend.availableFonts().value(1);
        QVERIFY(!selectedFont.isEmpty());
        backend.setEditorFont(selectedFont);
        backend.setTextScale(1.5);

        QCOMPARE(backend.editorFontSize(), 20);
        QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 22);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 24);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 36);
        QCOMPARE(editor->property("font").value<QFont>().family(), selectedFont);
        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 22);
        QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 20);
        QCOMPARE(QSettings().value(QStringLiteral("editor/fontSize")).toInt(), 20);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 30);
        QCOMPARE(Backend().editorFont(), selectedFont);
        QCOMPARE(Backend().editorFontSize(), 20);
    }

    void persistsEditorFontSizeAcrossBackendInstances() {
        {
            Backend backend;
            QCOMPARE(backend.editorFontSize(), 20);

            QSignalSpy changedSpy(&backend, &Backend::editorFontSizeChanged);
            backend.setEditorFontSize(28);
            QCOMPARE(changedSpy.count(), 1);
            QCOMPARE(QSettings().value(QStringLiteral("editor/fontSize")).toInt(), 28);
        }

        Backend restoredBackend;
        QCOMPARE(restoredBackend.editorFontSize(), 28);

        restoredBackend.setEditorFontSize(100);
        QCOMPARE(restoredBackend.editorFontSize(), 48);
        restoredBackend.setEditorFontSize(0);
        QCOMPARE(restoredBackend.editorFontSize(), 10);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    static void sendTouchpadWheel(QQuickWindow *window, const QPointF &position,
                                  int pixelDeltaY, int angleDeltaY,
                                  Qt::ScrollPhase phase) {
        QWheelEvent event(position, window->mapToGlobal(position),
                          QPoint(0, pixelDeltaY), QPoint(0, angleDeltaY), Qt::NoButton,
                          Qt::NoModifier, phase, false);
        QCoreApplication::sendEvent(window, &event);
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
