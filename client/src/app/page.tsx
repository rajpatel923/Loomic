import LoginExperience from "@/components/LoginExperience";

export default function Home() {
  return (
    <main className="flex min-h-0 flex-1 items-center justify-center overflow-auto px-4 py-10 sm:px-6">
      <div className="w-full max-w-sm">
        <LoginExperience />
      </div>
    </main>
  );
}
