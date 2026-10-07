#import <UIKit/UIKit.h>

#include "maxps4_backend.h"

@interface MaxPS4IntegratedViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UIButton *testButton;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) NSURL *selectedURL;
@property(nonatomic, assign) BOOL bootInProgress;
@property(nonatomic, assign) NSUInteger bootGeneration;
@end

@implementation MaxPS4IntegratedViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;

    UILabel *title = [[UILabel alloc] init];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    title.text = @"MaxPS4";
    title.font = [UIFont boldSystemFontOfSize:34.0];

    UILabel *subtitle = [[UILabel alloc] init];
    subtitle.translatesAutoresizingMaskIntoConstraints = NO;
    subtitle.text = @"iPhone • shadPS4/FEXCore integration build";
    subtitle.textColor = UIColor.secondaryLabelColor;
    subtitle.numberOfLines = 0;

    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel.numberOfLines = 0;
    self.statusLabel.text = @"Backend intégré. Active le JIT avec StikDebug, puis teste le backend ou importe ton propre eboot.bin / SELF.";
    self.statusLabel.font = [UIFont systemFontOfSize:15.0];

    self.testButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.testButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.testButton setTitle:@"Tester le backend shadPS4/FEX" forState:UIControlStateNormal];
    self.testButton.titleLabel.font = [UIFont boldSystemFontOfSize:18.0];
    [self.testButton addTarget:self action:@selector(runBackendTest) forControlEvents:UIControlEventTouchUpInside];

    self.importButton = [UIButton buttonWithType:UIButtonTypeSystem];
    self.importButton.translatesAutoresizingMaskIntoConstraints = NO;
    [self.importButton setTitle:@"Importer un eboot.bin / SELF" forState:UIControlStateNormal];
    self.importButton.titleLabel.font = [UIFont boldSystemFontOfSize:18.0];
    [self.importButton addTarget:self action:@selector(pickExecutable) forControlEvents:UIControlEventTouchUpInside];

    UILabel *legal = [[UILabel alloc] init];
    legal.translatesAutoresizingMaskIntoConstraints = NO;
    legal.numberOfLines = 0;
    legal.font = [UIFont systemFontOfSize:12.0];
    legal.textColor = UIColor.secondaryLabelColor;
    legal.text = @"Aucun jeu, firmware, clé ou contenu PlayStation n’est inclus. Utilise uniquement des fichiers que tu es autorisé à utiliser. Les licences et notices tierces sont incluses dans l’app.";

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        title, subtitle, self.statusLabel, self.testButton, self.importButton, legal
    ]];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 22.0;

    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:24.0],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-24.0],
        [stack.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor]
    ]];
}

- (void)runBackendTest {
    self.testButton.enabled = NO;
    self.statusLabel.text = @"Test du backend en cours…";
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL ok = maxps4_backend_self_test();
        const char *detail = maxps4_backend_diagnostic();
        NSString *text = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";
        dispatch_async(dispatch_get_main_queue(), ^{
            self.statusLabel.text = ok
                ? [NSString stringWithFormat:@"Backend shadPS4/FEX : OK\n%@", text]
                : [NSString stringWithFormat:@"Backend shadPS4/FEX : échec\n%@", text];
            self.testButton.enabled = YES;
        });
    });
}

- (void)pickExecutable {
    if (self.bootInProgress) {
        self.statusLabel.text = @"Un guest FEX est déjà en cours. Ferme puis relance MaxPS4 avant d’en lancer un autre.";
        return;
    }
    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.data"]
                                                               inMode:UIDocumentPickerModeOpen];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
  didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url = urls.firstObject;
    if (!url) return;

    BOOL scoped = [url startAccessingSecurityScopedResource];
    BOOL valid = maxps4_backend_validate_executable(url.fileSystemRepresentation);
    const char *detail = maxps4_backend_diagnostic();
    NSString *detailText = detail ? [NSString stringWithUTF8String:detail] : @"aucun détail";

    if (!valid) {
        self.statusLabel.text = [NSString stringWithFormat:@"Fichier refusé\n%@", detailText];
        if (scoped) [url stopAccessingSecurityScopedResource];
        return;
    }

    self.bootInProgress = YES;
    self.bootGeneration += 1;
    const NSUInteger generation = self.bootGeneration;
    self.importButton.enabled = NO;
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:url.path error:nil];
    unsigned long long fileSize = [attrs[NSFileSize] unsignedLongLongValue];
    self.statusLabel.text = [NSString stringWithFormat:
        @"Fichier sélectionné : %@\nTaille : %llu octets\n%@\nExécution FEX lancée en arrière-plan…",
        url.lastPathComponent, fileSize, detailText];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        BOOL booted = maxps4_backend_boot(url.fileSystemRepresentation);
        const char *bootDetail = maxps4_backend_diagnostic();
        NSString *bootText = bootDetail ? [NSString stringWithUTF8String:bootDetail] : @"aucun détail";
        if (scoped) [url stopAccessingSecurityScopedResource];

        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self.bootGeneration) return;
            self.bootInProgress = NO;
            self.importButton.enabled = YES;
            if (booted) {
                self.statusLabel.text = [NSString stringWithFormat:@"Démarrage demandé : %@\nTaille : %llu octets\n%@", url.lastPathComponent, fileSize, bootText];
            } else {
                self.statusLabel.text = [NSString stringWithFormat:
                    @"%@ validé.\nTaille : %llu octets\n%@\n\nLe handoff loader → FEX a été tenté. Cette build de test n’implémente pas encore tous les services/HLE PS4 nécessaires.",
                    url.lastPathComponent, fileSize, bootText];
            }
        });
    });

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (self.bootInProgress && generation == self.bootGeneration) {
            self.statusLabel.text = [NSString stringWithFormat:
                @"%@ validé.\nFEX est toujours en cours après 5 s.\n\nL’interface reste active : le guest semble bloqué dans l’exécution. Aucun arrêt forcé n’est tenté pour éviter de corrompre l’état du runtime.",
                url.lastPathComponent];
            self.importButton.enabled = NO;
        }
    });
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
