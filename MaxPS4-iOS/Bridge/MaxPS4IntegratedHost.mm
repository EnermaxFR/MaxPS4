#import <UIKit/UIKit.h>

#include "maxps4_backend.h"

@interface MaxPS4IntegratedViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *stateValueLabel;
@property(nonatomic, strong) UILabel *fileValueLabel;
@property(nonatomic, strong) UIButton *testButton;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) UIButton *copyButton;
@property(nonatomic, strong) NSTimer *diagnosticTimer;
@property(nonatomic, strong) NSDate *bootStartedAt;
@property(nonatomic, strong) NSURL *selectedURL;
@property(nonatomic, copy) NSString *lastDiagnostic;
@property(nonatomic, assign) unsigned long long selectedFileSize;
@property(nonatomic, assign) BOOL bootInProgress;
@property(nonatomic, assign) NSUInteger bootGeneration;
@end

@implementation MaxPS4IntegratedViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    self.view.backgroundColor = [UIColor colorWithRed:0.015 green:0.035 blue:0.085 alpha:1.0];
    self.lastDiagnostic = @"Backend intégré. Active le JIT avec StikDebug, puis teste le backend ou importe ton propre eboot.bin / SELF.";

    UIScrollView *scroll = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.alwaysBounceVertical = YES;
    scroll.showsVerticalScrollIndicator = NO;

    UIView *content = [[UIView alloc] init];
    content.translatesAutoresizingMaskIntoConstraints = NO;

    [self.view addSubview:scroll];
    [scroll addSubview:content];

    UILabel *brand = [[UILabel alloc] init];
    brand.translatesAutoresizingMaskIntoConstraints = NO;
    brand.attributedText = [self brandText];
    brand.font = [UIFont systemFontOfSize:42.0 weight:UIFontWeightBlack];
    brand.accessibilityLabel = @"MaxPS4";

    UILabel *subtitle = [[UILabel alloc] init];
    subtitle.translatesAutoresizingMaskIntoConstraints = NO;
    subtitle.text = @"iPhone • shadPS4 / FEXCore • Integration 0.8";
    subtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.58];
    subtitle.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightMedium];

    UIView *backendCard = [self cardView];
    UIStackView *backendStack = [[UIStackView alloc] init];
    backendStack.translatesAutoresizingMaskIntoConstraints = NO;
    backendStack.axis = UILayoutConstraintAxisHorizontal;
    backendStack.alignment = UIStackViewAlignmentCenter;
    backendStack.spacing = 12.0;

    UIView *dot = [[UIView alloc] init];
    dot.translatesAutoresizingMaskIntoConstraints = NO;
    dot.backgroundColor = [UIColor colorWithRed:0.20 green:0.90 blue:0.45 alpha:1.0];
    dot.layer.cornerRadius = 7.0;
    dot.layer.shadowColor = dot.backgroundColor.CGColor;
    dot.layer.shadowRadius = 8.0;
    dot.layer.shadowOpacity = 0.8;
    dot.layer.shadowOffset = CGSizeZero;

    UILabel *backendTitle = [[UILabel alloc] init];
    backendTitle.text = @"Version intégrée chargée";
    backendTitle.textColor = UIColor.whiteColor;
    backendTitle.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];

    UILabel *backendSubtitle = [[UILabel alloc] init];
    backendSubtitle.text = @"Backend shadPS4/FEXCore conservé";
    backendSubtitle.textColor = [UIColor colorWithWhite:1.0 alpha:0.56];
    backendSubtitle.font = [UIFont systemFontOfSize:13.0];

    UIStackView *backendText = [[UIStackView alloc] initWithArrangedSubviews:@[backendTitle, backendSubtitle]];
    backendText.axis = UILayoutConstraintAxisVertical;
    backendText.spacing = 3.0;

    UILabel *check = [[UILabel alloc] init];
    check.text = @"✓";
    check.textAlignment = NSTextAlignmentCenter;
    check.textColor = [UIColor colorWithRed:0.20 green:0.90 blue:0.45 alpha:1.0];
    check.font = [UIFont systemFontOfSize:24.0 weight:UIFontWeightBold];

    [backendStack addArrangedSubview:dot];
    [backendStack addArrangedSubview:backendText];
    [backendStack addArrangedSubview:check];
    [backendCard addSubview:backendStack];

    [NSLayoutConstraint activateConstraints:@[
        [dot.widthAnchor constraintEqualToConstant:14.0],
        [dot.heightAnchor constraintEqualToConstant:14.0],
        [backendStack.leadingAnchor constraintEqualToAnchor:backendCard.leadingAnchor constant:18.0],
        [backendStack.trailingAnchor constraintEqualToAnchor:backendCard.trailingAnchor constant:-18.0],
        [backendStack.topAnchor constraintEqualToAnchor:backendCard.topAnchor constant:16.0],
        [backendStack.bottomAnchor constraintEqualToAnchor:backendCard.bottomAnchor constant:-16.0],
    ]];

    self.importButton = [self actionButtonWithTitle:@"Importer un eboot.bin / SELF"
                                          subtitle:@"Sélectionne un exécutable PS4 autorisé"
                                           primary:YES];
    [self.importButton addTarget:self action:@selector(pickExecutable) forControlEvents:UIControlEventTouchUpInside];

    self.testButton = [self actionButtonWithTitle:@"Tester le backend shadPS4/FEX"
                                        subtitle:@"Vérifie le cœur intégré et son état"
                                         primary:NO];
    [self.testButton addTarget:self action:@selector(runBackendTest) forControlEvents:UIControlEventTouchUpInside];

    UIView *detailsCard = [self cardView];
    UILabel *detailsTitle = [[UILabel alloc] init];
    detailsTitle.translatesAutoresizingMaskIntoConstraints = NO;
    detailsTitle.text = @"Exécution";
    detailsTitle.textColor = UIColor.whiteColor;
    detailsTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UILabel *stateLabel = [self mutedLabel:@"État"];
    self.stateValueLabel = [self valueLabel:@"Prêt"];
    UILabel *fileLabel = [self mutedLabel:@"Fichier"];
    self.fileValueLabel = [self valueLabel:@"Aucun"];

    UIStackView *stateRow = [self infoRowWithLeft:stateLabel right:self.stateValueLabel];
    UIStackView *fileRow = [self infoRowWithLeft:fileLabel right:self.fileValueLabel];

    UIStackView *detailStack = [[UIStackView alloc] initWithArrangedSubviews:@[
        detailsTitle, stateRow, fileRow
    ]];
    detailStack.translatesAutoresizingMaskIntoConstraints = NO;
    detailStack.axis = UILayoutConstraintAxisVertical;
    detailStack.spacing = 13.0;
    [detailsCard addSubview:detailStack];

    [NSLayoutConstraint activateConstraints:@[
        [detailStack.leadingAnchor constraintEqualToAnchor:detailsCard.leadingAnchor constant:16.0],
        [detailStack.trailingAnchor constraintEqualToAnchor:detailsCard.trailingAnchor constant:-16.0],
        [detailStack.topAnchor constraintEqualToAnchor:detailsCard.topAnchor constant:16.0],
        [detailStack.bottomAnchor constraintEqualToAnchor:detailsCard.bottomAnchor constant:-16.0],
    ]];

    UILabel *diagnosticTitle = [[UILabel alloc] init];
    diagnosticTitle.text = @"Diagnostic live";
    diagnosticTitle.textColor = UIColor.whiteColor;
    diagnosticTitle.font = [UIFont systemFontOfSize:18.0 weight:UIFontWeightBold];

    UIView *diagnosticCard = [self cardView];
    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.text = self.lastDiagnostic;
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:13.0 weight:UIFontWeightRegular];
    self.statusLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.76];

    self.copyButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.copyButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.copyButton setTitle:@"Copier le diagnostic" forState:UIControlStateNormal];
    [self.copyButton setTitleColor:[UIColor colorWithRed:0.30 green:0.75 blue:1.0 alpha:1.0] forState:UIControlStateNormal];
    self.copyButton.titleLabel.font = [UIFont systemFontOfSize:14.0 weight:UIFontWeightSemibold];
    [self.copyButton addTarget:self action:@selector(copyDiagnostic) forControlEvents:UIControlEventTouchUpInside];

    [diagnosticCard addSubview:self.statusLabel];
    [diagnosticCard addSubview:self.copyButton];

    [NSLayoutConstraint activateConstraints:@[
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:diagnosticCard.leadingAnchor constant:16.0],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:diagnosticCard.trailingAnchor constant:-16.0],
        [self.statusLabel.topAnchor constraintEqualToAnchor:diagnosticCard.topAnchor constant:16.0],
        [self.copyButton.leadingAnchor constraintEqualToAnchor:diagnosticCard.leadingAnchor constant:16.0],
        [self.copyButton.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:12.0],
        [self.copyButton.bottomAnchor constraintEqualToAnchor:diagnosticCard.bottomAnchor constant:-14.0],
    ]];

    UILabel *legal = [[UILabel alloc] init];
    legal.translatesAutoresizingMaskIntoConstraints = NO;
    legal.numberOfLines = 0;
    legal.font = [UIFont systemFontOfSize:12.0];
    legal.textColor = [UIColor colorWithWhite:1.0 alpha:0.42];
    legal.text = @"Aucun jeu, firmware, clé ou contenu PlayStation n’est inclus. Utilise uniquement des fichiers que tu es autorisé à utiliser. Les licences et notices tierces sont incluses dans l’app.";

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        brand,
        subtitle,
        backendCard,
        self.importButton,
        self.testButton,
        detailsCard,
        diagnosticTitle,
        diagnosticCard,
        legal
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 16.0;

    [content addSubview:stack];

    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        [content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor],
        [content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor],
        [content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor],
        [content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor],

        [stack.leadingAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.leadingAnchor constant:20.0],
        [stack.trailingAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.trailingAnchor constant:-20.0],
        [stack.topAnchor constraintEqualToAnchor:content.safeAreaLayoutGuide.topAnchor constant:18.0],
        [stack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-28.0],
    ]];
}

- (NSAttributedString *)brandText {
    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:@"MaxPS4"];
    [text addAttribute:NSForegroundColorAttributeName
                 value:UIColor.whiteColor
                 range:NSMakeRange(0, 3)];
    [text addAttribute:NSForegroundColorAttributeName
                 value:[UIColor colorWithRed:0.10 green:0.64 blue:1.0 alpha:1.0]
                 range:NSMakeRange(3, 3)];
    return text;
}

- (UIView *)cardView {
    UIView *view = [[UIView alloc] init];
    view.translatesAutoresizingMaskIntoConstraints = NO;
    view.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.055];
    view.layer.cornerRadius = 22.0;
    view.layer.borderWidth = 1.0;
    view.layer.borderColor = [UIColor colorWithRed:0.08 green:0.42 blue:1.0 alpha:0.24].CGColor;
    return view;
}

- (UIButton *)actionButtonWithTitle:(NSString *)title
                            subtitle:(NSString *)subtitle
                             primary:(BOOL)primary {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    button.titleLabel.numberOfLines = 0;
    button.titleLabel.font = [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold];

    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:title
                                                                              attributes:@{
        NSForegroundColorAttributeName: UIColor.whiteColor,
        NSFontAttributeName: [UIFont systemFontOfSize:17.0 weight:UIFontWeightSemibold]
    }];
    [text appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@"\n%@", subtitle]
                                                                 attributes:@{
        NSForegroundColorAttributeName: [UIColor colorWithWhite:1.0 alpha:0.63],
        NSFontAttributeName: [UIFont systemFontOfSize:13.0 weight:UIFontWeightRegular]
    }]];
    [button setAttributedTitle:text forState:UIControlStateNormal];

    button.contentEdgeInsets = UIEdgeInsetsMake(16.0, 18.0, 16.0, 18.0);
    button.layer.cornerRadius = 22.0;
    button.layer.borderWidth = 1.0;

    if (primary) {
        button.backgroundColor = [UIColor colorWithRed:0.02 green:0.36 blue:0.95 alpha:1.0];
        button.layer.borderColor = [UIColor colorWithRed:0.10 green:0.72 blue:1.0 alpha:0.65].CGColor;
        button.layer.shadowColor = [UIColor colorWithRed:0.0 green:0.38 blue:1.0 alpha:0.45].CGColor;
        button.layer.shadowOpacity = 0.45;
        button.layer.shadowRadius = 16.0;
        button.layer.shadowOffset = CGSizeMake(0, 7);
    } else {
        button.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.045];
        button.layer.borderColor = [UIColor colorWithRed:0.10 green:0.50 blue:1.0 alpha:0.24].CGColor;
    }
    return button;
}

- (UILabel *)mutedLabel:(NSString *)text {
    UILabel *label = [[UILabel alloc] init];
    label.text = text;
    label.textColor = [UIColor colorWithWhite:1.0 alpha:0.50];
    label.font = [UIFont systemFontOfSize:13.0];
    return label;
}

- (UILabel *)valueLabel:(NSString *)text {
    UILabel *label = [[UILabel alloc] init];
    label.text = text;
    label.textColor = UIColor.whiteColor;
    label.textAlignment = NSTextAlignmentRight;
    label.font = [UIFont systemFontOfSize:13.0 weight:UIFontWeightSemibold];
    label.numberOfLines = 2;
    return label;
}

- (UIStackView *)infoRowWithLeft:(UIView *)left right:(UIView *)right {
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[left, right]];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 12.0;
    return row;
}

- (void)setDiagnostic:(NSString *)text {
    self.lastDiagnostic = text ?: @"aucun diagnostic";
    self.statusLabel.text = self.lastDiagnostic;
}

- (void)copyDiagnostic {
    UIPasteboard.generalPasteboard.string = self.lastDiagnostic ?: self.statusLabel.text ?: @"";
    NSString *oldTitle = self.copyButton.currentTitle;
    [self.copyButton setTitle:@"Copié ✓" forState:UIControlStateNormal];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.2 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [self.copyButton setTitle:oldTitle ?: @"Copier le diagnostic" forState:UIControlStateNormal];
    });
}

- (void)runBackendTest {
    if (self.bootInProgress) {
        [self setDiagnostic:@"Un guest FEX est en cours. Attends sa fin avant de lancer le self-test."];
        return;
    }

    self.testButton.enabled = NO;
    self.stateValueLabel.text = @"Test…";
    [self setDiagnostic:@"Test du backend shadPS4/FEX en cours…"];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL ok = maxps4_backend_self_test();
        const char *detail = maxps4_backend_diagnostic();
        NSString *text = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";

        dispatch_async(dispatch_get_main_queue(), ^{
            self.stateValueLabel.text = ok ? @"Backend OK" : @"Échec backend";
            [self setDiagnostic:(ok
                ? [NSString stringWithFormat:@"Backend shadPS4/FEX : OK\n%@", text]
                : [NSString stringWithFormat:@"Backend shadPS4/FEX : échec\n%@", text])];
            self.testButton.enabled = YES;
        });
    });
}

- (void)pickExecutable {
    if (self.bootInProgress) {
        [self setDiagnostic:@"Un guest FEX est déjà en cours. Attends la fin de l’exécution avant d’en lancer un autre."];
        return;
    }

    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.data"]
                                                               inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)startDiagnosticPollingForGeneration:(NSUInteger)generation {
    [self.diagnosticTimer invalidate];
    self.diagnosticTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                           repeats:YES
                                                             block:^(NSTimer *timer) {
        if (!self.bootInProgress || generation != self.bootGeneration) {
            [timer invalidate];
            return;
        }

        char liveBuf[1024] = {};
        maxps4_backend_live_diagnostic(liveBuf, sizeof(liveBuf));
        NSString *live = [NSString stringWithUTF8String:liveBuf] ?: @"aucun diagnostic live";
        NSTimeInterval elapsed = self.bootStartedAt ? -[self.bootStartedAt timeIntervalSinceNow] : 0;

        self.stateValueLabel.text = [NSString stringWithFormat:@"FEX • %.0f s", elapsed];
        [self setDiagnostic:[NSString stringWithFormat:
            @"%@\n\nDiagnostic live (%.0f s) :\n%@",
            self.fileValueLabel.text ?: @"Exécutable validé",
            elapsed,
            live]];
    }];
}

- (void)stopDiagnosticPolling {
    [self.diagnosticTimer invalidate];
    self.diagnosticTimer = nil;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
  didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (!url) return;

    BOOL scoped = [url startAccessingSecurityScopedResource];

    NSError *attrError = nil;
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:url.path error:&attrError];
    unsigned long long fileSize = [attrs[NSFileSize] unsignedLongLongValue];

    BOOL valid = maxps4_backend_validate_executable(url.fileSystemRepresentation);
    const char *detail = maxps4_backend_diagnostic();
    NSString *detailText = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";

    self.selectedURL = url;
    self.selectedFileSize = fileSize;
    self.fileValueLabel.text = [NSString stringWithFormat:@"%@ • %llu o", url.lastPathComponent, fileSize];

    if (!valid) {
        self.stateValueLabel.text = @"Refusé";
        [self setDiagnostic:[NSString stringWithFormat:
            @"Fichier refusé : %@\nTaille : %llu octets\n%@",
            url.lastPathComponent,
            fileSize,
            detailText]];
        if (scoped) [url stopAccessingSecurityScopedResource];
        return;
    }

    self.bootInProgress = YES;
    self.bootGeneration += 1;
    const NSUInteger generation = self.bootGeneration;
    self.bootStartedAt = [NSDate date];

    self.importButton.enabled = NO;
    self.testButton.enabled = NO;
    self.stateValueLabel.text = @"Validation OK";

    [self setDiagnostic:[NSString stringWithFormat:
        @"Fichier sélectionné : %@\nTaille : %llu octets\nValidation : %@\n\nHandoff loader → FEX en cours…",
        url.lastPathComponent,
        fileSize,
        detailText]];

    [self startDiagnosticPollingForGeneration:generation];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL booted = maxps4_backend_boot(url.fileSystemRepresentation);
        const char *bootDetail = maxps4_backend_diagnostic();
        NSString *bootText = bootDetail ? [NSString stringWithUTF8String:bootDetail] : @"aucun détail";
        if (scoped) [url stopAccessingSecurityScopedResource];

        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self.bootGeneration) return;

            [self stopDiagnosticPolling];
            self.bootInProgress = NO;
            self.importButton.enabled = YES;
            self.testButton.enabled = YES;

            NSTimeInterval elapsed = self.bootStartedAt ? -[self.bootStartedAt timeIntervalSinceNow] : 0;

            if (booted) {
                self.stateValueLabel.text = @"Terminé";
                [self setDiagnostic:[NSString stringWithFormat:
                    @"Exécution terminée : %@\nTaille : %llu octets\nDurée : %.2f s\n\n%@",
                    url.lastPathComponent,
                    fileSize,
                    elapsed,
                    bootText]];
            } else {
                self.stateValueLabel.text = @"Handoff arrêté";
                [self setDiagnostic:[NSString stringWithFormat:
                    @"%@ validé.\nTaille : %llu octets\nDurée : %.2f s\n\n%@\n\nLe handoff loader → FEX a été tenté. Le diagnostic ci-dessus indique précisément le dernier stade atteint ou le service/HLE manquant.",
                    url.lastPathComponent,
                    fileSize,
                    elapsed,
                    bootText]];
            }
        });
    });
}

- (void)dealloc {
    [self stopDiagnosticPolling];
}
@end

@interface MaxPS4IntegratedAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation MaxPS4IntegratedAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[MaxPS4IntegratedViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MaxPS4IntegratedAppDelegate class]));
    }
}
