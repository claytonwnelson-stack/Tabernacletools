// Tabernacle Tools "Share" extension (optional target WR_SHARE_EXTENSION). Takes ONLY what the user shares: the selected text
// (or the page text Safari hands over via WRPreprocess.js), plain text, or a text / ChordPro / PDF file. It never fetches
// a URL - a bare link is refused with a hint to select the chords instead.
// Hand-off: free Apple IDs can't use App Groups, so the text goes on the general pasteboard under a private type
// (com.tabernacletools.shared-chart). Tabernacle Tools picks it up the next time it is opened and shows the import preview.
#import <UIKit/UIKit.h>
#import <PDFKit/PDFKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface ShareViewController : UIViewController
@end

@implementation ShareViewController {
    UILabel* _label;
}

- (void) viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithWhite: 0.08 alpha: 1];
    _label = [[UILabel alloc] initWithFrame: CGRectInset (self.view.bounds, 24, 24)];
    _label.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _label.numberOfLines = 0; _label.textAlignment = NSTextAlignmentCenter; _label.textColor = UIColor.whiteColor;
    _label.font = [UIFont systemFontOfSize: 20 weight: UIFontWeightSemibold];
    _label.text = @"Reading the chart...";
    [self.view addSubview: _label];
    [self collect];
}

- (void) finishWith: (NSString*) text {
    dispatch_async (dispatch_get_main_queue(), ^{
        NSString* t = [text stringByTrimmingCharactersInSet: NSCharacterSet.whitespaceAndNewlineCharacterSet];
        BOOL urlOnly = [t rangeOfString: @"\n"].location == NSNotFound && ([t hasPrefix: @"http://"] || [t hasPrefix: @"https://"]);
        if (t.length == 0 || urlOnly) {
            self->_label.text = @"Nothing to import.\n\nTabernacle Tools doesn't download web pages. Select the chords on the page, then Share (or Copy and use Paste chart).";
        } else {
            NSData* d = [t dataUsingEncoding: NSUTF8StringEncoding];
            [UIPasteboard.generalPasteboard setItems: @[ @{ @"com.tabernacletools.shared-chart": d, (NSString*) UTTypePlainText.identifier: t } ] options: @{}];
            self->_label.text = @"Chart ready.\n\nOpen Tabernacle Tools - you'll see the import preview to check the title, key and sections.";
        }
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (2.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self.extensionContext completeRequestReturningItems: @[] completionHandler: nil]; });
    });
}

- (void) collect {
    NSExtensionItem* item = self.extensionContext.inputItems.firstObject;
    for (NSItemProvider* p in item.attachments) {
        if ([p hasItemConformingToTypeIdentifier: (NSString*) UTTypePropertyList.identifier]) {   // Safari + WRPreprocess.js
            [p loadItemForTypeIdentifier: (NSString*) UTTypePropertyList.identifier options: nil completionHandler: ^(NSDictionary* d, NSError* e) {
                NSDictionary* js = d[NSExtensionJavaScriptPreprocessingResultsKey];
                NSString* sel = js[@"selection"], *page = js[@"pageText"], *title = js[@"title"];
                NSString* body = sel.length > 0 ? sel : page;
                [self finishWith: title.length > 0 && body.length > 0 ? [NSString stringWithFormat: @"%@\n\n%@", title, body] : body]; }];
            return;
        }
        if ([p hasItemConformingToTypeIdentifier: (NSString*) UTTypePDF.identifier]) {
            [p loadItemForTypeIdentifier: (NSString*) UTTypePDF.identifier options: nil completionHandler: ^(id data, NSError* e) {
                PDFDocument* doc = [data isKindOfClass: NSURL.class] ? [[PDFDocument alloc] initWithURL: data] : [[PDFDocument alloc] initWithData: data];
                [self finishWith: doc.string ?: @""]; }];
            return;
        }
        if ([p hasItemConformingToTypeIdentifier: (NSString*) UTTypeText.identifier]) {   // selected text, .txt / .cho files
            [p loadItemForTypeIdentifier: (NSString*) UTTypeText.identifier options: nil completionHandler: ^(id data, NSError* e) {
                NSString* s = [data isKindOfClass: NSString.class] ? data
                            : [data isKindOfClass: NSURL.class] ? [NSString stringWithContentsOfURL: data encoding: NSUTF8StringEncoding error: nil]
                            : [data isKindOfClass: NSData.class] ? [[NSString alloc] initWithData: data encoding: NSUTF8StringEncoding] : @"";
                [self finishWith: s ?: @""]; }];
            return;
        }
    }
    [self finishWith: @""];   // only a URL (or something else): never fetched
}
@end
